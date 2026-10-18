[English](README.md) | [简体中文](README.zh-CN.md) | [日本語](README.ja.md) | [繁體中文](README.zh-TW.md)

# TempHumi Monitor 溫濕度監測系統

以 STM32F103C8T6 為基礎的溫濕度擷取系統，搭配 Qt 桌面程式、明確的分通道資料來源、原始資料記錄，以及 Windows 可攜版與安裝版。

桌面版本 **v010.18.1**，MCU 韌體 **v010.17.1**。功能運作、模擬資料及獨立物理精度分別驗證。

## 主要功能

- 真實溫度擷取與三種**暫定** MCU 溫度修正方法，同時保留原始平均值及發布值。
- MCU 溫度與濕度可分別明確選擇 REAL/SIM，支援真實溫度加 MCU 模擬濕度；真實故障不會無聲切換為模擬。
- TH3 序列埠、增長檔案及 REPLAY 輸入，獨立縱軸、資料點與事件檢視、SHA-256 驗證的完整 Session 匯出。
- 暫停顯示仍持續記錄；結束 Session 完成檔案儲存並釋放連接埠。F1 提供離線說明，About 區分應用程式原始碼身分及實際回應的韌體版本。
- 自動重新連線與已確認設定還原預設啟用，間隔5秒、最多12次；變更設定前核驗最新裝置身分、韌體及讀回結果。

## 硬體與執行需求

STM32F103C8T6、ADS1115、NTC/U6 溫度調理、CHS1101/TLC556/OPA330 濕度設計、OLED 與 CH340 USB-UART。已驗證的 Windows 程式需要 Windows10/11 x64 及適用的 CH340 驅動程式；燒錄使用 ST-Link/SWD。電路圖代表設計意圖，不等於實體導通驗證。

[Hardware evidence](docs/hardware/FINAL_TECHNICAL_AUDIT.md) · [Architecture](docs/architecture/OVERVIEW.md)

## 軟體架構

HAL／平台 → BSP → Driver → 純 Algorithm → Application／資料來源選擇 → Protocol。OLED 與 UART 使用相同發布語意，Qt 不重新校正。韌體啟動預設為 REAL/REAL。

## 建置與韌體燒錄

已驗證工具為 Qt5.14.2/MinGW7.3x64、STM32CubeIDE1.19.0/Arm GCC13.3、Python3 與 STM32CubeProgrammer。可匯入 .project/.cproject 或執行下列命令。燒錄前確認目標、供電、映像來源及擷取已停止，保存完整 Flash 備份；BIN 起始位址為0x08000000，寫入後執行 Verify 並逐位元組比較讀回。韌體符合64KiB Flash／20KiB RAM 限制。

```powershell
python tools/build_desktop.py --configuration both --label verification --build-root build_verify/qt --evidence build_verify/qt_logs
python tools/build_mcu.py --build-root build_verify/mcu --evidence build_verify/mcu_logs
python tools/run_regressions.py --build-root build_verify/tests --evidence build_verify/test_logs --desktop-objects build_verify/qt/qt_debug/debug
```

[Build / flash instructions](docs/development/BUILD.md)

## Windows 可攜版與安裝版

從 Release 下載可攜 ZIP 或未簽署 Setup。請完整解壓縮後執行 TempHumi.exe；安裝版應選擇可寫入目錄，可建立開始功能表及桌面捷徑。兩者使用相同的已驗證執行階段，安裝不會自動連線或燒錄。解除安裝保留非安裝程式擁有的資料及使用者設定，請勿僅移動 EXE 而遺漏 Qt 程式庫及外掛。

[Release](https://github.com/HanamiGekka/STM32-TempHumi-Monitor/releases/tag/v010.18.1) · [Desktop guide](docs/development/DESKTOP.md)

## 擷取與通訊協定

動態識別實際連接埠，使用115200/8N1／無流量控制。啟動僅準備 SERIAL，不自動連線；選擇記錄位置後開始。資料來源與溫度修正須明確 Apply 並由 MCU 確認。TH3 為23欄位，包含原始碼值、單位、有效性、錯誤、分通道來源及世代。顯示視窗為5–1200秒、最多24000點；原始／CSV 記錄不受顯示裁切影響。

[TH3](docs/protocol/TH2_TH3.md) · [UART commands](docs/protocol/UART_COMMANDS.md)

## 測試與驗證

本輪 MCU／Qt Debug、Release 完整建置均為零警告、零錯誤；22個主機回歸套件、100%／150%各94項原生 GUI 檢查、120項可攜封裝檢查與102項 bundle 檢查通過。可攜與安裝程式分別要求至少3600秒真實硬體測試；最終狀態及計數見驗證報告。偶發錯誤與復原記錄會保留並個別統計。

使用者提供的十組溫度比較：Raw MAE0.754℃、RMSE1.020725℃；修正後 MAE0.522℃、RMSE0.720777℃，最大絕對誤差1.53℃，8/10點位於±1℃。此樣本不構成全量程計量精度保證。

[Validation](docs/release/VALIDATION.md)

## 已知限制

- 真實濕度維持 **PARTIAL / NOT VALIDATED**；模擬濕度不能證明真實 RH 精度。
- PWM 波形、頻率、工作週期為 **NOT TESTED**；V_CHS1101 測點近零值與跟隨器／ADC 的直流讀值仍有矛盾。
- USB 偶發異常的物理根因為 **UNRESOLVED**；復原記錄不能證明線材、供電或焊點故障。
- 溫度修正仍為暫定方法；不宣稱全量程獨立精度、新的 OLED 光學測量或另一台實體 Windows 電腦已驗證。

## 發行與原始碼身分

桌面 v010.18.1 從經審查的公開提交建置；MCU v010.17.1 保持已驗證映像位元組。重新建置會嵌入當時提交及時間，不冒充同版本正式替換。獨立公開歷史透過生產輸入雜湊、凍結原始碼及資產 SHA-256 建立對應關係，不包含原始 Session、裝置標識、私有歷史、Board 照片或含帳號資訊的 CAD 資料庫。

[Source / asset identity](docs/release/SOURCE_IDENTITY.json)

## 授權條款與貢獻

專案自有原始碼及文件採 **GPL-3.0-or-later**；QCustomPlot 作者為 Emanuel Eichhammer，Qt/ST/Arm/MinGW 保留各自條款。原同學 Qt 程式與硬體協作均標示為團隊貢獻，使用者已確認成員公開授權。Qt 對應原始碼封裝與執行階段聲明隨 Release 提供。

[LICENSE](LICENSE) · [NOTICE](NOTICE.md) · [Dependency terms](docs/release/THIRD_PARTY_NOTICES.md)
