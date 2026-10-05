/*
 * SPDX-FileCopyrightText: 2022-2022 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include "bamboo.h"
#include "bambooconfig.h"
#include "surroundingtracker.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fcitx-config/iniparser.h>
#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/charutils.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/macros.h>
#include <fcitx-utils/misc.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx-utils/stringutils.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/action.h>
#include <fcitx/addoninstance.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/inputpanel.h>
#include <fcitx/menu.h>
#include <fcitx/statusarea.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>
#include <fcitx/userinterfacemanager.h>
#include <cctype>
#include <fcntl.h>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fcitx {

namespace {

constexpr std::string_view MacroPrefix = "macro/";
constexpr std::string_view InputMethodActionPrefix = "bamboo-input-method-";
constexpr std::string_view CharsetActionPrefix = "bamboo-charset-";
const std::string CustomKeymapFile = "conf/bamboo-custom-keymap.conf";

FCITX_DEFINE_LOG_CATEGORY(bamboo, "bamboo");

std::string macroFile(std::string_view imName) {
    return stringutils::concat("conf/bamboo-macro-", imName, ".conf");
}

uintptr_t newMacroTable(const BambooMacroTable &macroTable) {
    std::vector<char *> charArray;
    RawConfig r;
    macroTable.save(r);
    for (const auto &keymap : *macroTable.macros) {
        charArray.push_back(const_cast<char *>(keymap.key->data()));
        charArray.push_back(const_cast<char *>(keymap.value->data()));
    }
    charArray.push_back(nullptr);
    return NewMacroTable(charArray.data());
}

std::vector<std::string> convertToStringList(char **array) {
    std::vector<std::string> result;
    for (int i = 0; array[i]; i++) {
        result.push_back(array[i]);
        free(array[i]);
    }
    free(array);
    return result;
}

bool isValidEditState(uint32_t state) {
    constexpr uint32_t controlMask = 1 << 2;
    constexpr uint32_t mod1Mask = 1 << 3;
    constexpr uint32_t superMask = 1 << 26;
    constexpr uint32_t hyperMask = 1 << 27;
    constexpr uint32_t metaMask = 1 << 28;
    return (state & (controlMask | mod1Mask | superMask | hyperMask |
                     metaMask)) == 0;
}

std::u32string toUCS4(std::string_view text) {
    std::u32string result;
    if (!utf8::validate(text)) {
        return result;
    }
    auto iter = utf8::MakeUTF8CharIterator(text.begin(), text.end());
    const auto end = utf8::MakeUTF8CharIterator(text.end(), text.end());
    for (; iter != end; ++iter) {
        result.push_back(static_cast<char32_t>(*iter));
    }
    return result;
}

std::string fromUCS4(const std::u32string &text) {
    std::string result;
    for (auto chr : text) {
        result += utf8::UCS4ToUTF8(chr);
    }
    return result;
}

bamboo_reedit::TextView currentTextView(const SurroundingText &surroundingText) {
    if (!surroundingText.isValid() ||
        !utf8::validate(surroundingText.text())) {
        return {};
    }
    return bamboo_reedit::makeTextView(toUCS4(surroundingText.text()),
                                surroundingText.cursor(),
                                surroundingText.anchor());
}

} // namespace

#define FCITX_BAMBOO_DEBUG() FCITX_LOGC(bamboo, Debug)

class BambooState final : public InputContextProperty {
public:
    BambooState(BambooEngine *engine, InputContext *ic)
        : engine_(engine), ic_(ic) {
        setEngine();
    }

    ~BambooState() {}

    void setEngine() {
        bambooEngine_.reset();

        if (*engine_->config().inputMethod == "Custom") {
            std::vector<char *> charArray;
            for (const auto &keymap : *engine_->customKeymap().customKeymap) {
                charArray.push_back(const_cast<char *>(keymap.key->data()));
                FCITX_INFO() << charArray.back();
                charArray.push_back(const_cast<char *>(keymap.value->data()));
                FCITX_INFO() << charArray.back();
            }
            charArray.push_back(nullptr);
            bambooEngine_.reset(NewCustomEngine(charArray.data(),
                                                engine_->dictionary(),
                                                engine_->macroTable()));
        } else {
            bambooEngine_.reset(NewEngine(engine_->config().inputMethod->data(),
                                          engine_->dictionary(),
                                          engine_->macroTable()));
        }
        setOption();
    }

    void setOption() {
        if (!bambooEngine_) {
            return;
        }
        FcitxBambooEngineOption option = {
            .autoNonVnRestore = *engine_->config().autoNonVnRestore,
            .ddFreeStyle = true,
            .macroEnabled = *engine_->config().macro,
            .autoCapitalizeMacro = *engine_->config().capitalizeMacro,
            .spellCheckWithDicts = *engine_->config().spellCheck,
            .outputCharset = engine_->config().outputCharset->data(),
            .modernStyle = *engine_->config().modernStyle,
            .freeMarking = *engine_->config().freeMarking,
        };
        EngineSetOption(bambooEngine_.handle(), &option);
    }

    void keyEvent(KeyEvent &keyEvent) {
        if (!bambooEngine_) {
            return;
        }
        // Ignore all key release.
        if (keyEvent.isRelease()) {
            return;
        }
        if (keyEvent.rawKey().check(FcitxKey_Shift_L) ||
            keyEvent.rawKey().check(FcitxKey_Shift_R)) {
            return;
        }

        if (tryReeditPreviousWord(keyEvent)) {
            return;
        }

        if (keyEvent.key().checkKeyList(*engine_->config().restoreKeyStroke)) {
            EngineSetRestoreKeyStroke(bambooEngine_.handle());
            keyEvent.filterAndAccept();
            return;
        }

        if (EngineProcessKeyEvent(bambooEngine_.handle(),
                                  keyEvent.rawKey().sym(),
                                  keyEvent.rawKey().states())) {
            keyEvent.filterAndAccept();
        } else {
            trackPassThroughKey(keyEvent.rawKey());
        }

        flushEngineOutput();
    }

    void reset() {
        tracker_.unknownChange();
        ic_->inputPanel().reset();
        if (bambooEngine_) {
            ResetEngine(bambooEngine_.handle());
        }
        ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
        ic_->updatePreedit();
    }

    void commitBuffer() {
        ic_->inputPanel().reset();
        if (bambooEngine_) {
            // The reason that we do not commit here is we want to force the
            // behavior. When client get unfocused, the framework will try to
            // commit the string.
            EngineCommitPreedit(bambooEngine_.handle());
            UniqueCPtr<char> commit(EnginePullCommit(bambooEngine_.handle()));
            if (commit && commit.get()[0]) {
                tracker_.inserted(toUCS4(commit.get()));
                ic_->commitString(commit.get());
            }
        }
        ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
        ic_->updatePreedit();
    }

    void surroundingTextUpdated() {
        tracker_.clientUpdated(currentTextView(ic_->surroundingText()));
    }

private:
    void trackPassThroughKey(const Key &key) {
        if (key.isModifier()) {
            return;
        }
        const auto states = static_cast<uint32_t>(key.states());
        if (key.check(FcitxKey_BackSpace)) {
            tracker_.backspaced();
            return;
        }
        const auto chr = Key::keySymToUnicode(key.sym());
        if (isValidEditState(states) && chr >= 0x20 && chr != 0x7f) {
            tracker_.inserted(std::u32string(1, static_cast<char32_t>(chr)));
            return;
        }
        tracker_.unknownChange();
    }

    void flushEngineOutput() {
        if (char *commit = EnginePullCommit(bambooEngine_.handle())) {
            if (commit[0]) {
                tracker_.inserted(toUCS4(commit));
                ic_->commitString(commit);
            }
            free(commit);
        }

        ic_->inputPanel().reset();
        UniqueCPtr<char> preedit(EnginePullPreedit(bambooEngine_.handle()));
        if (preedit && preedit.get()[0]) {
            std::string_view preeditView = preedit.get();
            Text text;
            TextFormatFlags format;
            if (*engine_->config().displayUnderline) {
                format = TextFormatFlag::Underline;
            }
            if (utf8::validate(preeditView)) {
                text.append(std::string(preeditView), format);
            }
            text.setCursor(text.textLength());

            if (ic_->capabilityFlags().test(CapabilityFlag::Preedit)) {
                ic_->inputPanel().setClientPreedit(text);
            } else {
                ic_->inputPanel().setPreedit(text);
            }
        }
        ic_->updatePreedit();
        ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
    }

    // UniKey-style editing of an already committed word: when nothing is
    // being composed and the cursor sits right after a word, pull that word
    // back into the preedit. Any doubt (stale or unsupported surrounding
    // text) makes this a no-op.
    bool tryReeditPreviousWord(KeyEvent &keyEvent) {
        if (!*engine_->config().editPreviousWord ||
            EngineHasPreedit(bambooEngine_.handle()) ||
            !ic_->capabilityFlags().test(CapabilityFlag::SurroundingText)) {
            return false;
        }
        const auto sym = keyEvent.rawKey().sym();
        const auto states = keyEvent.rawKey().states();
        if (!isValidEditState(states)) {
            return false;
        }
        const bool asciiLetter = (sym >= FcitxKey_a && sym <= FcitxKey_z) ||
                                 (sym >= FcitxKey_A && sym <= FcitxKey_Z);
        if (!asciiLetter &&
            !EngineCanProcessKey(bambooEngine_.handle(), sym, states)) {
            return false;
        }

        const auto *view = tracker_.verified();
        if (!view || *view != currentTextView(ic_->surroundingText())) {
            return false;
        }
        auto wordUcs4 = bamboo_reedit::wordEndingAtCursor(*view);
        if (!wordUcs4) {
            return false;
        }
        const auto word = fromUCS4(*wordUcs4);
        const auto wordLength = wordUcs4->size();

        if (!EngineRestoreWord(bambooEngine_.handle(), word.c_str())) {
            ResetEngine(bambooEngine_.handle());
            return false;
        }
        UniqueCPtr<char> restored(EnginePullPreedit(bambooEngine_.handle()));
        if (!restored || word != restored.get()) {
            ResetEngine(bambooEngine_.handle());
            return false;
        }
        if (!EngineProcessKeyEvent(bambooEngine_.handle(), sym, states)) {
            ResetEngine(bambooEngine_.handle());
            return false;
        }

        tracker_.deletedBefore(wordLength);
        ic_->deleteSurroundingText(-static_cast<int>(wordLength),
                                   static_cast<int>(wordLength));
        keyEvent.filterAndAccept();
        flushEngineOutput();
        return true;
    }

    BambooEngine *engine_;
    InputContext *ic_;
    CGoObject bambooEngine_;
    bamboo_reedit::SurroundingTracker tracker_;
};

BambooEngine::BambooEngine(Instance *instance)
    : instance_(instance), factory_([this](InputContext &ic) {
          return new BambooState(this, &ic);
      }) {
    Init();
    {
        auto imNames = convertToStringList(GetInputMethodNames());
        imNames.push_back("Custom");
        imNames_ = std::move(imNames);
    }
    if (std::find(imNames_.begin(), imNames_.end(), "Telex") ==
        imNames_.end()) {
        throw std::runtime_error("Failed to find required input method Telex");
    }
    FCITX_BAMBOO_DEBUG() << "Supported input methods: " << imNames_;
    config_.inputMethod.annotation().setList(imNames_);

    auto fd = StandardPaths::global().open(StandardPathsType::PkgData,
                                           "bamboo/vietnamese.cm.dict");
    if (!fd.isValid()) {
        throw std::runtime_error("Failed to load dictionary");
    }
    dictionary_.reset(NewDictionary(fd.release()));

    auto &uiManager = instance_->userInterfaceManager();
    inputMethodAction_ = std::make_unique<SimpleAction>();
    inputMethodAction_->setIcon("document-edit");
    inputMethodAction_->setShortText(_("Input Method"));
    uiManager.registerAction("bamboo-input-method", inputMethodAction_.get());

    inputMethodMenu_ = std::make_unique<Menu>();
    inputMethodAction_->setMenu(inputMethodMenu_.get());
    for (const auto &imName : imNames_) {
        inputMethodSubAction_.emplace_back(std::make_unique<SimpleAction>());
        auto *action = inputMethodSubAction_.back().get();
        action->setShortText(imName);
        action->setCheckable(true);
        uiManager.registerAction(
            stringutils::concat(InputMethodActionPrefix, imName), action);
        connections_.emplace_back(action->connect<SimpleAction::Activated>(
            [this, imName](InputContext *ic) {
                if (config_.inputMethod.value() == imName) {
                    return;
                }
                config_.inputMethod.setValue(imName);
                saveConfig();
                refreshEngine();
                updateInputMethodAction(ic);
            }));

        inputMethodMenu_->addAction(action);
    }

    charsetAction_ = std::make_unique<SimpleAction>();
    charsetAction_->setShortText(_("Output charset"));
    charsetAction_->setIcon("character-set");
    uiManager.registerAction("bamboo-charset", charsetAction_.get());
    charsetMenu_ = std::make_unique<Menu>();
    charsetAction_->setMenu(charsetMenu_.get());

    auto charsets = convertToStringList(GetCharsetNames());
    for (const auto &charset : charsets) {
        charsetSubAction_.emplace_back(std::make_unique<SimpleAction>());
        auto *action = charsetSubAction_.back().get();
        action->setShortText(charset);
        action->setCheckable(true);
        connections_.emplace_back(action->connect<SimpleAction::Activated>(
            [this, charset](InputContext *ic) {
                if (config_.outputCharset.value() == charset) {
                    return;
                }
                config_.outputCharset.setValue(charset);
                saveConfig();
                refreshEngine();
                updateCharsetAction(ic);
            }));
        uiManager.registerAction(
            stringutils::concat(CharsetActionPrefix, charset), action);
        charsetMenu_->addAction(action);
    }
    config_.outputCharset.annotation().setList(charsets);

    spellCheckAction_ = std::make_unique<SimpleAction>();
    spellCheckAction_->setLongText(_("Spell check"));
    spellCheckAction_->setIcon("tools-check-spelling");
    connections_.emplace_back(
        spellCheckAction_->connect<SimpleAction::Activated>(
            [this](InputContext *ic) {
                config_.spellCheck.setValue(!*config_.spellCheck);
                saveConfig();
                refreshOption();
                updateSpellAction(ic);
            }));
    uiManager.registerAction("bamboo-spell-check", spellCheckAction_.get());
    macroAction_ = std::make_unique<SimpleAction>();
    macroAction_->setLongText(_("Macro"));
    macroAction_->setIcon("edit-find");
    connections_.emplace_back(macroAction_->connect<SimpleAction::Activated>(
        [this](InputContext *ic) {
            config_.macro.setValue(!*config_.macro);
            saveConfig();
            refreshOption();
            updateMacroAction(ic);
        }));
    uiManager.registerAction("bamboo-macro", macroAction_.get());

    reloadConfig();
    instance_->inputContextManager().registerProperty("bambooState", &factory_);
    surroundingTextWatcher_ = instance_->watchEvent(
        EventType::InputContextSurroundingTextUpdated,
        EventWatcherPhase::PostInputMethod, [this](Event &event) {
            auto &icEvent = static_cast<InputContextEvent &>(event);
            icEvent.inputContext()
                ->propertyFor(&factory_)
                ->surroundingTextUpdated();
        });
}

void BambooEngine::reloadConfig() {
    readAsIni(config_, "conf/bamboo.conf");
    readAsIni(customKeymap_, CustomKeymapFile);
    for (const auto &imName : imNames_) {
        auto &table = macroTables_[imName];
        readAsIni(table, macroFile(imName));
        macroTableObject_[imName].reset(newMacroTable(table));
    }

    populateConfig();
}

const Configuration *BambooEngine::getSubConfig(const std::string &path) const {
    if (path == "custom_keymap") {
        return &customKeymap_;
    }
    if (path.starts_with(MacroPrefix)) {
        const auto imName = path.substr(MacroPrefix.size());
        if (auto iter = macroTables_.find(imName); iter != macroTables_.end()) {
            return &iter->second;
        }
        return nullptr;
    }
    return nullptr;
}

void BambooEngine::setConfig(const RawConfig &config) {
    config_.load(config, true);
    saveConfig();
    populateConfig();
}

void BambooEngine::populateConfig() {
    refreshEngine();
    refreshOption();
    updateMacroAction(nullptr);
    updateSpellAction(nullptr);
    updateInputMethodAction(nullptr);
    updateCharsetAction(nullptr);
}

void BambooEngine::setSubConfig(const std::string &path,
                                const RawConfig &config) {
    if (path == "custom_keymap") {
        customKeymap_.load(config, true);
        safeSaveAsIni(customKeymap_, CustomKeymapFile);
        refreshEngine();
    } else if (path.starts_with(MacroPrefix)) {
        const auto imName = path.substr(MacroPrefix.size());
        if (auto iter = macroTables_.find(imName); iter != macroTables_.end()) {
            iter->second.load(config, true);
            safeSaveAsIni(iter->second, macroFile(imName));
            macroTableObject_[imName].reset(newMacroTable(iter->second));
            refreshEngine();
        }
    }
}

std::string BambooEngine::subMode(const fcitx::InputMethodEntry & /*entry*/,
                                  fcitx::InputContext & /*inputContext*/) {
    return *config_.inputMethod;
}

void BambooEngine::activate(const InputMethodEntry &entry,
                            InputContextEvent &event) {
    FCITX_UNUSED(entry);
    FCITX_UNUSED(event);
    auto &statusArea = event.inputContext()->statusArea();

    updateMacroAction(event.inputContext());
    updateSpellAction(event.inputContext());
    updateInputMethodAction(event.inputContext());
    updateCharsetAction(event.inputContext());

    statusArea.addAction(StatusGroup::InputMethod, inputMethodAction_.get());
    statusArea.addAction(StatusGroup::InputMethod, charsetAction_.get());
    statusArea.addAction(StatusGroup::InputMethod, spellCheckAction_.get());
    statusArea.addAction(StatusGroup::InputMethod, macroAction_.get());
}

void BambooEngine::deactivate(const InputMethodEntry &entry,
                              InputContextEvent &event) {
    FCITX_UNUSED(entry);
    auto *state = event.inputContext()->propertyFor(&factory_);
    if (event.type() != EventType::InputContextFocusOut) {
        state->commitBuffer();
    } else {
        state->reset();
    }
}

void BambooEngine::keyEvent(const InputMethodEntry &entry, KeyEvent &keyEvent) {
    FCITX_UNUSED(entry);
    auto *state = keyEvent.inputContext()->propertyFor(&factory_);

    state->keyEvent(keyEvent);
}

void BambooEngine::reset(const InputMethodEntry &entry,
                         InputContextEvent &event) {
    FCITX_UNUSED(entry);
    auto *state = event.inputContext()->propertyFor(&factory_);
    state->reset();
}

void BambooEngine::refreshEngine() {
    FCITX_BAMBOO_DEBUG() << "Refresh engine";
    if (!factory_.registered()) {
        return;
    }

    instance_->inputContextManager().foreach([this](InputContext *ic) {
        auto *state = ic->propertyFor(&factory_);
        state->setEngine();
        if (ic->hasFocus()) {
            state->reset();
        }
        return true;
    });
}

void BambooEngine::refreshOption() {
    if (!factory_.registered()) {
        return;
    }
    instance_->inputContextManager().foreach([this](InputContext *ic) {
        auto *state = ic->propertyFor(&factory_);
        state->setOption();
        if (ic->hasFocus()) {
            state->reset();
        }
        return true;
    });
}

void BambooEngine::updateSpellAction(InputContext *ic) {
    spellCheckAction_->setChecked(*config_.spellCheck);
    spellCheckAction_->setShortText(*config_.spellCheck
                                        ? _("Spell Check Enabled")
                                        : _("Spell Check Disabled"));
    if (ic) {
        spellCheckAction_->update(ic);
    }
}

void BambooEngine::updateMacroAction(InputContext *ic) {
    macroAction_->setChecked(*config_.macro);
    macroAction_->setShortText(*config_.macro ? _("Macro Enabled")
                                              : _("Macro Disabled"));
    if (ic) {
        macroAction_->update(ic);
    }
}

void BambooEngine::updateInputMethodAction(InputContext *ic) {
    auto name =
        stringutils::concat(InputMethodActionPrefix, *config_.inputMethod);
    for (const auto &action : inputMethodSubAction_) {
        action->setChecked(action->name() == name);
        if (ic) {
            action->update(ic);
        }
    }
}

void BambooEngine::updateCharsetAction(InputContext *ic) {
    auto name =
        stringutils::concat(CharsetActionPrefix, *config_.outputCharset);
    for (const auto &action : charsetSubAction_) {
        action->setChecked(action->name() == name);
        if (ic) {
            action->update(ic);
        }
    }
}

} // namespace fcitx

FCITX_ADDON_FACTORY_V2(bamboo, fcitx::BambooFactory)
