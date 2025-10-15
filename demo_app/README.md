# Dự án: Hệ thống IoT Quan trắc Cảm biến (PlatformIO)

Đây là một hệ thống IoT đơn giản sử dụng ESP32 để quan trắc dữ liệu từ các cảm biến và gửi về một dashboard thời gian thực qua giao thức MQTT. Dự án được cấu trúc để sử dụng với PlatformIO trong Visual Studio Code.

## Cấu trúc thư mục

Ngoài các file và thư mục do PlatformIO tự tạo (`.pio`, `.vscode`), cấu trúc chính của dự án bao gồm:

```
demo_app/
├── platformio.ini       # File cấu hình chính cho PlatformIO
├── src/
│   └── main.cpp         # Mã nguồn chính cho ESP32
├── server/
│   └── index.html       # File dashboard cho trình duyệt
├── lib/                 # (Tùy chọn) Nơi chứa các thư viện lập trình riêng
└── README.md            # File tài liệu này
```

- **`platformio.ini`**: File quan trọng nhất, định nghĩa board, framework, và các thư viện cần thiết. PlatformIO sẽ dựa vào file này để quản lý dự án.
- **`/src`**: Thư mục chứa toàn bộ mã nguồn bạn sẽ viết cho vi điều khiển (ESP32).
- **`/server`**: Chứa mã nguồn cho trang web dashboard (HTML, CSS, JS).
- **`/lib`**: Nếu bạn có các thư viện tự viết hoặc tải về thủ công, hãy đặt chúng ở đây.

## Phần cứng

- Board phát triển ESP32.
- Cảm biến nhiệt độ, độ ẩm (ví dụ: DHT22).
- Cảm biến ánh sáng (ví dụ: Quang trở + điện trở 10kΩ).
- Dây cắm và breadboard.

## Hướng dẫn cài đặt

1.  **Cài đặt môi trường**:
    - Cài đặt [Visual Studio Code](https://code.visualstudio.com/).
    - Cài đặt extension [PlatformIO IDE](https://platformio.org/install/ide?install=vscode) từ VS Code Marketplace.

2.  **Mở dự án**:
    - Mở VS Code.
    - Chọn `File > Open Folder...` và trỏ đến thư mục `demo_app`.
    - PlatformIO sẽ tự động nhận diện file `platformio.ini` và cài đặt các thư viện đã được định nghĩa trong `lib_deps`.

3.  **Nạp code cho ESP32**:
    - Kết nối board ESP32 với máy tính qua cổng USB.
    - Trong VS Code, mở file `src/main.cpp`.
    - Sử dụng các nút chức năng của PlatformIO trên thanh trạng thái ở dưới cùng:
        - Nhấn vào nút **Build** (biểu tượng dấu tick) để biên dịch code.
        - Nhấn vào nút **Upload** (biểu tượng mũi tên) để nạp chương trình vào ESP32.

4.  **Sử dụng Dashboard**:
    - Mở file `server/index.html` bằng trình duyệt web để xem dữ liệu.
