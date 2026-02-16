@echo off
setlocal enabledelayedexpansion

REM ============================================================
REM  CodecSim インストーラー ビルドスクリプト
REM  Full版とTrial版のインストーラーEXEを Inno Setup で生成する
REM ============================================================

REM -- 作業ディレクトリをこのバッチファイルの場所に設定 --
cd /d "%~dp0"

echo.
echo ============================================================
echo  CodecSim Installer Build Script
echo ============================================================
echo.

REM -- 成功/失敗カウンター初期化 --
set "FAIL_COUNT=0"
set "SUCCESS_COUNT=0"

REM ============================================================
REM  手順1: Inno Setup コンパイラ (ISCC.exe) の検出
REM ============================================================
echo [1/4] Inno Setup コンパイラを検索中...

set "ISCC="

REM -- 一般的なインストールパスを順に確認 --
if exist "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" (
    set "ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
)
if exist "C:\Program Files\Inno Setup 6\ISCC.exe" (
    set "ISCC=C:\Program Files\Inno Setup 6\ISCC.exe"
)

REM -- PATH上にISCC.exeがあるか確認 --
if not defined ISCC (
    where ISCC.exe >nul 2>&1
    if !errorlevel! equ 0 (
        for /f "delims=" %%i in ('where ISCC.exe') do set "ISCC=%%i"
    )
)

REM -- 見つからなかった場合はエラー終了 --
if not defined ISCC (
    echo [エラー] Inno Setup 6 が見つかりません。
    echo         以下のいずれかにインストールされていることを確認してください:
    echo           - C:\Program Files (x86)\Inno Setup 6\
    echo           - C:\Program Files\Inno Setup 6\
    echo         または ISCC.exe を PATH に追加してください。
    exit /b 1
)

echo         検出: "%ISCC%"
echo.

REM ============================================================
REM  手順2: ビルド成果物ディレクトリの存在確認
REM  インストーラーに含めるファイルが揃っているかチェックする
REM ============================================================
echo [2/4] ビルド成果物の存在を確認中...

set "FULL_READY=1"
set "TRIAL_READY=1"

REM -- Full版のVST3バンドルが存在するか確認 --
if not exist "..\build\out\CodecSim.vst3\" (
    echo [警告] Full版のビルド成果物が見つかりません: ..\build\out\CodecSim.vst3\
    echo         Full版インストーラーのビルドをスキップします。
    set "FULL_READY=0"
) else (
    echo         Full版 VST3 バンドル: OK
)

REM -- Trial版のVST3バンドルが存在するか確認 --
if not exist "..\build-trial\out\CodecSim.vst3\" (
    echo [警告] Trial版のビルド成果物が見つかりません: ..\build-trial\out\CodecSim.vst3\
    echo         Trial版インストーラーのビルドをスキップします。
    set "TRIAL_READY=0"
) else (
    echo         Trial版 VST3 バンドル: OK
)

REM -- 両方とも見つからない場合はエラー終了 --
if "%FULL_READY%"=="0" if "%TRIAL_READY%"=="0" (
    echo.
    echo [エラー] ビルド成果物が一つも見つかりません。
    echo         先に CMake ビルドを実行してください。
    exit /b 1
)

echo.

REM ============================================================
REM  手順3: Full版インストーラーのビルド
REM ============================================================
if "%FULL_READY%"=="1" (
    echo [3/4] Full版インストーラーをビルド中...
    echo         スクリプト: CodecSim_Full.iss
    echo.

    "%ISCC%" "CodecSim_Full.iss"

    if !errorlevel! neq 0 (
        echo.
        echo [エラー] Full版インストーラーのビルドに失敗しました。
        set /a FAIL_COUNT+=1
    ) else (
        echo.
        echo         Full版インストーラー: ビルド成功
        set /a SUCCESS_COUNT+=1
    )
) else (
    echo [3/4] Full版インストーラー: スキップ (ビルド成果物なし)
)

echo.

REM ============================================================
REM  手順4: Trial版インストーラーのビルド
REM ============================================================
if "%TRIAL_READY%"=="1" (
    echo [4/4] Trial版インストーラーをビルド中...
    echo         スクリプト: CodecSim_Trial.iss
    echo.

    "%ISCC%" "CodecSim_Trial.iss"

    if !errorlevel! neq 0 (
        echo.
        echo [エラー] Trial版インストーラーのビルドに失敗しました。
        set /a FAIL_COUNT+=1
    ) else (
        echo.
        echo         Trial版インストーラー: ビルド成功
        set /a SUCCESS_COUNT+=1
    )
) else (
    echo [4/4] Trial版インストーラー: スキップ (ビルド成果物なし)
)

echo.

REM ============================================================
REM  結果サマリー
REM ============================================================
echo ============================================================
echo  ビルド結果サマリー
echo ============================================================
echo   成功: %SUCCESS_COUNT%
echo   失敗: %FAIL_COUNT%
echo.

REM -- 出力先の案内 --
REM   Full版:  OutputDir は .iss 内で ..\build\installer に指定済み
REM   Trial版: OutputDir 未指定のため Inno Setup 既定の Output\ に出力される
if "%FULL_READY%"=="1" if "%FAIL_COUNT%"=="0" (
    echo  出力ファイル:
    echo    Full版:  ..\build\installer\CodecSim_Setup.exe
    echo    Trial版: Output\CodecSimTrial_Setup.exe
    echo.
)

REM -- 失敗があった場合はエラーコード1で終了 --
if %FAIL_COUNT% gtr 0 (
    echo [結果] 一部のビルドに失敗しました。上記のエラーを確認してください。
    exit /b 1
)

echo [結果] すべてのインストーラーのビルドが正常に完了しました。
exit /b 0
