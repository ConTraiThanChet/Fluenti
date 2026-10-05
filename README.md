# Fluenti — fork of fcitx5-bamboo

Bộ gõ tiếng Việt cho Fcitx 5, fork từ [fcitx/fcitx5-bamboo](https://github.com/fcitx/fcitx5-bamboo),
hướng tới trải nghiệm gõ giống **UniKey trên Windows**.

> A Vietnamese input method for Fcitx 5, forked from fcitx5-bamboo, aiming for a
> UniKey-on-Windows typing experience.

> [!NOTE]
> Fluenti **chưa có** trên kho chính thức (Arch repo, AUR, Debian, Fedora…).
> Hiện tại cần build từ mã nguồn theo hướng dẫn bên dưới.

## Cải tiến so với fcitx5-bamboo

### 1. Sửa tiếp chữ đã gõ (kiểu UniKey)

Đã gõ xong một chữ và bấm cách, bạn vẫn có thể quay lại sửa dấu:

```
Đang␣  →  Backspace  →  w  →  Đăng
sao␣   →  Backspace  →  s  →  sáo
```

Hoạt động mỗi khi con trỏ đứng ngay sau một chữ (sau khi xóa dấu cách, dùng phím
mũi tên, hoặc click chuột) và bạn gõ tiếp một chữ cái: chữ trước con trỏ được
lấy lại vào vùng soạn thảo để tiếp tục bỏ dấu.

Cơ chế an toàn để **không bao giờ lặp/chèn rác chữ**:

- Dùng surrounding text của ứng dụng; không gửi phím Backspace giả.
- Fluenti tự dự đoán nội dung quanh con trỏ sau mỗi thao tác (commit, gõ ký tự,
  Backspace, xóa chữ) và chỉ sửa khi báo cáo từ ứng dụng **khớp đúng** dự đoán.
  Báo cáo trễ/cũ bị bỏ qua — phím được gõ bình thường.
- Chỉ áp dụng cho chữ tối đa 7 ký tự, không có chữ số, con trỏ không nằm giữa
  chữ và không có vùng chọn.
- Ứng dụng không hỗ trợ surrounding text: tính năng tự tắt.

Tùy chọn: **Continue editing the word before the cursor** (`EditPreviousWord`,
mặc định bật).

### 2. Escape hủy chữ đang gõ

`Esc` (không kèm phím bổ trợ) xóa chữ đang soạn mà **không** commit.
`Ctrl+Esc`, `Alt+Esc`… vẫn được chuyển cho ứng dụng.

### 3. Kiểm thử

- Unit test Go cho engine (`bamboo/fcitxbambooengine_test.go`).
- Unit test C++ cho bộ theo dõi surrounding text (`test/`, chạy bằng `ctest`).
- Các file `*_test.go` được loại khỏi bản build c-archive.

## Cài đặt (build từ mã nguồn)

### 1. Cài phụ thuộc

Arch / CachyOS / Manjaro:

```sh
sudo pacman -S --needed fcitx5 fcitx5-configtool fcitx5-gtk fcitx5-qt \
    cmake extra-cmake-modules ninja go gettext git
```

Debian / Ubuntu:

```sh
sudo apt install fcitx5 fcitx5-config-qt libfcitx5core-dev libfcitx5config-dev \
    libfcitx5utils-dev fcitx5-modules-dev cmake extra-cmake-modules ninja-build \
    golang gettext git
```

Fcitx 5 cần bản **≥ 5.1.13**, Go **≥ 1.18**.

### 2. Gỡ fcitx5-bamboo chính thức (nếu có)

Fluenti cài đè cùng tên addon `bamboo`, nên gỡ gói cũ trước để tránh xung đột:

```sh
sudo pacman -R fcitx5-bamboo      # Arch
sudo apt remove fcitx5-bamboo     # Debian/Ubuntu
```

### 3. Build và cài

```sh
git clone --recursive https://github.com/ConTraiThanChet/Fluenti.git
cd Fluenti
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
ctest --test-dir build          # tùy chọn: chạy test
sudo ninja -C build install
```

> Fcitx 5 chỉ nạp addon từ prefix của nó (thường là `/usr`), nên cần
> `-DCMAKE_INSTALL_PREFIX=/usr`. Cài vào `~/.local` sẽ báo "bamboo not available".

### 4. Khởi động lại Fcitx 5 và bật bộ gõ

```sh
fcitx5-remote -e; sleep 1; fcitx5 -d
```

Mở `fcitx5-configtool` → **Add input method** → tìm **Bamboo** → thêm vào
danh sách.

### Cập nhật

```sh
cd Fluenti
git pull --recurse-submodules
cmake --build build && sudo ninja -C build install
fcitx5-remote -e; sleep 1; fcitx5 -d
```

### Gỡ cài đặt

```sh
sudo ninja -C build uninstall
```

## Cấu hình gợi ý

Trong `fcitx5-configtool` → Bamboo → Configure, hoặc file
`~/.config/fcitx5/conf/bamboo.conf`:

| Tùy chọn | Ý nghĩa |
|---|---|
| `EditPreviousWord` | Sửa tiếp chữ trước con trỏ (mặc định `True`) |
| `DisplayUnderline` | Gạch chân chữ đang soạn |
| `RestoreKeyStroke` | Phím khôi phục chữ gốc, ví dụ `Shift+space` |

## Hạn chế đã biết

- Chrome/Chromium trên Wayland có lỗi bỏ qua lệnh xóa surrounding text khi thanh
  địa chỉ đang hiện gợi ý tự hoàn thành. Nếu gặp lặp chữ ở ứng dụng nào, hãy tắt
  `EditPreviousWord`.
- Trong khi soạn chữ, ứng dụng vẫn có thể tự vẽ gạch chân cho vùng soạn thảo.

## Giấy phép và ghi công

- Dựa trên [fcitx5-bamboo](https://github.com/fcitx/fcitx5-bamboo) (CSSlayer)
  và [bamboo-core](https://github.com/BambooEngine/bamboo-core).
- Phát hành theo **LGPL-2.1-or-later**, giống bản gốc (xem `LICENSES/`).
