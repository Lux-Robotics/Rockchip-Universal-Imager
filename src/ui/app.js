let pollingEnabled = true;
let lastInfo = "";
let lastStatus = "disconnected";
let driverInstallRunning = false;
let flashRunning = false;
let selectedImagePath = "";
let logVisible = false;
let logLoaded = false;
let logCleared = false;
let calculatingUsedSpace = false;
const driverDeviceName = "Rockchip Bootloader Device";

const statusDot = document.getElementById("statusDot");
const statusText = document.getElementById("statusText");
const deviceInfo = document.getElementById("deviceInfo");
const storageInfo = document.getElementById("storageInfo");
const infoIcon = document.getElementById("infoIcon");
const pollingToggle = document.getElementById("pollingToggle");
const driverStatus = document.getElementById("driverStatus");
const installDriver = document.getElementById("installDriver");
const flashStatus = document.getElementById("flashStatus");
const flashProgress = document.getElementById("flashProgress");
const connectDevice = document.getElementById("connectDevice");
const selectImage = document.getElementById("selectImage");
const flashImage = document.getElementById("flashImage");
const eraseEmmc = document.getElementById("eraseEmmc");
const backupEmmc = document.getElementById("backupEmmc");
const calculateUsed = document.getElementById("calculateUsed");
const cancelFlash = document.getElementById("cancelFlash");
const selectedImage = document.getElementById("selectedImage");
const toggleLog = document.getElementById("toggleLog");
const logPanel = document.getElementById("logPanel");
const liveLog = document.getElementById("liveLog");
const copyLog = document.getElementById("copyLog");
const clearLog = document.getElementById("clearLog");
const confirmModal = document.getElementById("confirmModal");
const confirmMessage = document.getElementById("confirmMessage");
const confirmOkBtn = document.getElementById("confirmOkBtn");
const confirmCancelBtn = document.getElementById("confirmCancelBtn");
const alertModal = document.getElementById("alertModal");
const alertMessage = document.getElementById("alertMessage");
const alertOkBtn = document.getElementById("alertOkBtn");
const api = window.saucer && window.saucer.exposed ? window.saucer.exposed : null;

let currentOperation = null;

function logAction(message) {
    // logWrite already tags entries with the "ui" category server-side;
    // only prefix here for the in-app live-log panel display.
    if (api && api.logWrite) {
        api.logWrite(message);
    }
    if (window.appendLiveLog) {
        window.appendLiveLog("[ui] " + message);
    }
}

function showConfirm(message) {
    return new Promise((resolve) => {
        confirmMessage.textContent = message;
        confirmModal.style.display = "flex";
        const cleanup = (result) => {
            confirmModal.style.display = "none";
            confirmOkBtn.removeEventListener("click", onOk);
            confirmCancelBtn.removeEventListener("click", onCancel);
            resolve(result);
        };
        const onOk = () => cleanup(true);
        const onCancel = () => cleanup(false);
        confirmOkBtn.addEventListener("click", onOk);
        confirmCancelBtn.addEventListener("click", onCancel);
    });
}

function showAlert(message) {
    return new Promise((resolve) => {
        alertMessage.textContent = message;
        alertModal.style.display = "flex";
        const onOk = () => {
            alertModal.style.display = "none";
            alertOkBtn.removeEventListener("click", onOk);
            resolve();
        };
        alertOkBtn.addEventListener("click", onOk);
    });
}

function render() {
    const connected = lastStatus === "connected";
    const detected = lastStatus === "detected";
    const toolMissing = lastStatus === "tool_missing";
    statusDot.style.background = connected ? "#2fa84f" : (detected ? "#2a6fd9" : (toolMissing ? "#d9822b" : "#a33"));
    statusText.textContent = toolMissing ? "rkdeveloptool not found" : (detected ? "detected" : lastStatus);
    flashImage.disabled = flashRunning || !connected || !selectedImagePath;
    eraseEmmc.disabled = flashRunning || !connected;
    backupEmmc.disabled = flashRunning || !connected;
    calculateUsed.disabled = flashRunning || !connected || calculatingUsedSpace;
    connectDevice.style.display = detected ? "inline-block" : "none";
    connectDevice.disabled = flashRunning;
    cancelFlash.style.display = flashRunning ? "inline-block" : "none";
    if (connected || detected) {
        const text = lastInfo.trim() || (connected ? "device connected" : "device detected — press Connect");
        deviceInfo.textContent = text;
        infoIcon.title = text;
    } else if (toolMissing) {
        const text = "rkdeveloptool is missing from the app folder — reinstall or repair the app.";
        deviceInfo.textContent = text;
        infoIcon.title = text;
        if (!driverInstallRunning) {
            driverStatus.textContent = "";
        }
    } else {
        deviceInfo.textContent = "check usb";
        infoIcon.title = "check usb";
        if (!driverInstallRunning) {
            driverStatus.textContent = "";
        }
    }
    if (!connected) {
        storageInfo.textContent = "";
    }
}

function setDriverInstallRunning(running) {
    driverInstallRunning = running;
    if (installDriver) {
        installDriver.disabled = running;
    }
    if (running) {
        driverStatus.textContent = "Installing driver... (this may take a while)";
    }
}

let flashDotInterval = null;
let flashDotCount = 0;
let flashPercent = 0;
let flashBaseLabel = "Flashing";

function updateFlashStatusText() {
    const dots = ".".repeat(flashDotCount);
    // "Connecting" (bootloader download) doesn't report granular progress -
    // showing a static "0%" the whole time would look broken rather than
    // just quick, so only show the number for operations that actually
    // report one.
    const showPercent = currentOperation !== "connect";
    flashStatus.textContent = flashBaseLabel + dots + (showPercent ? " " + flashPercent + "%" : "");
}

function startFlashAnimation(label) {
    flashBaseLabel = label;
    flashDotCount = 0;
    flashPercent = 0;
    updateFlashStatusText();
    if (flashDotInterval) {
        clearInterval(flashDotInterval);
    }
    flashDotInterval = setInterval(() => {
        flashDotCount = (flashDotCount + 1) % 4;
        updateFlashStatusText();
    }, 400);
}

function stopFlashAnimation() {
    if (flashDotInterval) {
        clearInterval(flashDotInterval);
        flashDotInterval = null;
    }
}

function setFlashRunning(running) {
    flashRunning = running;
    selectImage.disabled = running;
    if (!running) {
        stopFlashAnimation();
    }
    render();
}

const platformReady = (api && api.isWindowsPlatform) ? api.isWindowsPlatform() : Promise.resolve(false);

async function refreshDriverInfo() {
    const isWindows = await platformReady;
    if (!isWindows || !api || !api.getUsbDriverInfo) {
        // libusb-win32/libwdi is a Windows-only concept; macOS and Linux use
        // libusb directly, so there's nothing to report here.
        return;
    }
    const info = await api.getUsbDriverInfo();
    if (!info.found) {
        driverStatus.textContent = info.error || "device not found";
        return;
    }
    if (info.ok) {
        driverStatus.textContent = "Driver: " + (info.driver || "libusb-win32");
    } else {
        driverStatus.textContent = info.error || ("Driver: " + (info.driver || "unknown"));
    }
}

async function refreshStorageInfo() {
    if (!api || !api.getStorageInfo) {
        return;
    }
    const info = await api.getStorageInfo();
    if (!info.success) {
        storageInfo.textContent = info.error || "";
        return;
    }
    storageInfo.textContent = "eMMC: " + info.emmc_gb + " GB";
}

window.updateDeviceStatus = (status) => {
    const changed = status !== lastStatus;
    lastStatus = status;
    render();
    if (changed && status === "connected") {
        refreshDriverInfo();
        refreshStorageInfo();
    }
};

window.updateDeviceInfo = (info) => {
    lastInfo = info || "";
    render();
};

pollingToggle.addEventListener("click", async () => {
    pollingEnabled = !pollingEnabled;
    pollingToggle.textContent = pollingEnabled ? "Pause Polling" : "Resume Polling";
    logAction("Polling " + (pollingEnabled ? "resumed" : "paused"));
    if (api && api.setPollingEnabled) {
        await api.setPollingEnabled(pollingEnabled);
    }
});

toggleLog.addEventListener("click", () => {
    logVisible = !logVisible;
    logPanel.style.display = logVisible ? "block" : "none";
    toggleLog.textContent = logVisible ? "Hide Log" : "Show Log";
    logAction("Log panel " + (logVisible ? "shown" : "hidden"));
    if (logVisible && api && api.getLogContents && !logLoaded && !logCleared) {
        api.getLogContents().then(result => {
            try {
                liveLog.value = (result && result.text) ? result.text : "";
                liveLog.scrollTop = liveLog.scrollHeight;
                logLoaded = true;
            } catch (e) {
                liveLog.value = "";
            }
        });
    }
});

copyLog.addEventListener("click", async () => {
    logAction("Copied log to clipboard");
    const text = liveLog.value || "";
    if (navigator.clipboard && navigator.clipboard.writeText) {
        await navigator.clipboard.writeText(text);
    } else {
        liveLog.select();
        document.execCommand("copy");
        liveLog.setSelectionRange(0, 0);
    }
});

clearLog.addEventListener("click", () => {
    liveLog.value = "";
    logCleared = true;
    logAction("Cleared live log view");
});

document.getElementById("testLog").addEventListener("click", async () => {
    const message = "[hardware-helper] Test log message";
    if (api && api.logWrite) {
        await api.logWrite(message);
    }
    window.appendLiveLog(message);
});

selectImage.addEventListener("click", async () => {
    logAction("Select .img clicked");
    if (!api || !api.selectImageFile) {
        flashStatus.textContent = "file picker unavailable";
        return;
    }
    const result = await api.selectImageFile();
    if (!result.success) {
        flashStatus.textContent = result.error || "file picker canceled";
        logAction("Image selection canceled: " + (result.error || "no file chosen"));
        return;
    }
    selectedImagePath = result.path;
    selectedImage.textContent = result.path;
    flashStatus.textContent = "image selected";
    logAction("Selected image: " + result.path);
    render();
});

flashImage.addEventListener("click", async () => {
    logAction("Flash Image clicked (file=" + selectedImagePath + ")");
    if (!api || !api.flashImage) {
        flashStatus.textContent = "flash unavailable";
        return;
    }
    if (flashRunning) {
        return;
    }
    currentOperation = "image";
    setFlashRunning(true);
    flashProgress.value = 0;
    startFlashAnimation("Flashing");
    const result = await api.flashImage(selectedImagePath);
    if (!result.started) {
        setFlashRunning(false);
        flashStatus.textContent = result.error || "flash failed";
        logAction("Flash Image failed to start: " + (result.error || "unknown error"));
    }
});

connectDevice.addEventListener("click", async () => {
    logAction("Connect clicked");
    if (!api || !api.flashBootloader) {
        flashStatus.textContent = "flash unavailable";
        return;
    }
    if (flashRunning) {
        return;
    }
    currentOperation = "connect";
    setFlashRunning(true);
    flashProgress.value = 0;
    startFlashAnimation("Connecting");
    const result = await api.flashBootloader();
    if (!result.started) {
        setFlashRunning(false);
        flashStatus.textContent = result.error || "connect failed";
        logAction("Connect failed to start: " + (result.error || "unknown error"));
    }
});

eraseEmmc.addEventListener("click", async () => {
    logAction("Erase eMMC clicked");
    if (!api || !api.eraseEmmc) {
        flashStatus.textContent = "erase unavailable";
        return;
    }
    if (flashRunning) {
        return;
    }
    const confirmed = await showConfirm(
        "This will permanently erase all data on the device's eMMC storage, including the flashed OS. " +
        "This cannot be undone. Continue?"
    );
    logAction("Erase eMMC " + (confirmed ? "confirmed" : "canceled by user"));
    if (!confirmed) {
        return;
    }
    currentOperation = "erase";
    setFlashRunning(true);
    flashProgress.value = 0;
    startFlashAnimation("Erasing eMMC");
    const result = await api.eraseEmmc();
    if (!result.started) {
        setFlashRunning(false);
        flashStatus.textContent = result.error || "erase failed";
        logAction("Erase eMMC failed to start: " + (result.error || "unknown error"));
    }
});

backupEmmc.addEventListener("click", async () => {
    logAction("Backup eMMC clicked");
    if (!api || !api.selectBackupDestination || !api.backupEmmc) {
        flashStatus.textContent = "backup unavailable";
        return;
    }
    if (flashRunning) {
        return;
    }
    const picked = await api.selectBackupDestination();
    if (!picked.success) {
        logAction("Backup destination selection canceled: " + (picked.error || "no path chosen"));
        return;
    }
    logAction("Backup destination: " + picked.path);
    currentOperation = "backup";
    setFlashRunning(true);
    flashProgress.value = 0;
    startFlashAnimation("Backing up eMMC");
    const result = await api.backupEmmc(picked.path);
    if (!result.started) {
        setFlashRunning(false);
        flashStatus.textContent = result.error || "backup failed";
        logAction("Backup eMMC failed to start: " + (result.error || "unknown error"));
    }
});

calculateUsed.addEventListener("click", async () => {
    logAction("Calculate Used Space clicked");
    if (!api || !api.calculateUsedSpace) {
        return;
    }
    if (calculatingUsedSpace || flashRunning) {
        return;
    }
    calculatingUsedSpace = true;
    // Strip any "Used: ..."/"Calculating..." suffix from a previous run so
    // repeat clicks don't stack onto each other.
    const baseText = storageInfo.textContent.split("  ·  ")[0];
    storageInfo.textContent = baseText + "  ·  Calculating...";
    render();
    const result = await api.calculateUsedSpace();
    calculatingUsedSpace = false;
    if (!result.success) {
        storageInfo.textContent = baseText;
        flashStatus.textContent = result.error || "calculate failed";
        logAction("Calculate Used Space failed: " + (result.error || "unknown error"));
        render();
        return;
    }
    const usedGb = result.used_gb.toFixed(1);
    storageInfo.textContent = baseText + "  ·  Used: ~" + usedGb + " GB";
    logAction("Calculated used space: ~" + usedGb + " GB");
    render();
});

cancelFlash.addEventListener("click", async () => {
    logAction("Cancel clicked");
    if (!flashRunning || !api || !api.cancelFlash) {
        return;
    }
    const confirmed = await showConfirm(
        "Canceling now will leave the operation incomplete and will likely invalidate the currently flashed OS. Continue?"
    );
    logAction("Cancel " + (confirmed ? "confirmed" : "dismissed"));
    if (!confirmed) {
        return;
    }
    await api.cancelFlash();
});

async function initDriverInstallUi() {
    if (!installDriver) {
        return;
    }
    const isWindows = await platformReady;
    if (!isWindows || !api || !api.installUsbDriver) {
        installDriver.style.display = "none";
        driverStatus.style.display = "none";
        return;
    }
    installDriver.addEventListener("click", async () => {
        logAction("Install libusb-win32 clicked");
        if (driverInstallRunning) {
            return;
        }
        setDriverInstallRunning(true);
        const result = await api.installUsbDriver(driverDeviceName);
        if (!result.started) {
            setDriverInstallRunning(false);
            driverStatus.textContent = result.error || "driver install already in progress";
        }
    });
}

initDriverInstallUi();

window.onDriverInstallComplete = (result) => {
    setDriverInstallRunning(false);
    if (!result || !result.success) {
        driverStatus.textContent = (result && result.error) || "driver install failed";
    } else {
        driverStatus.textContent = "driver installed";
    }
    refreshDriverInfo();
};

window.onFlashComplete = async (result) => {
    const operation = currentOperation;
    currentOperation = null;
    setFlashRunning(false);

    const label = operation === "erase" ? "Erase eMMC"
        : operation === "connect" ? "Connect"
        : operation === "backup" ? "Backup eMMC"
        : "Flash Image";

    if (result && result.cancelled) {
        flashStatus.textContent = "Flash canceled";
        logAction((operation || "Flash") + " canceled");
        await showAlert(label + " was canceled.");
        return;
    }
    if (!result || !result.success) {
        const error = (result && result.error) || "flash failed";
        flashStatus.textContent = error;
        logAction((operation || "Flash") + " failed: " + error);
        await showAlert(label + " failed: " + error);
        return;
    }

    logAction((operation || "Flash") + " completed successfully");
    if (operation === "connect") {
        // The bootloader download is an internal step, not a user-facing
        // "flash" — the status dot turning green (once polling picks up
        // Loader mode) is the only feedback needed for a successful connect.
        flashStatus.textContent = "";
    } else if (operation === "erase") {
        flashStatus.textContent = "Erase completed";
        await showAlert("Erase eMMC completed successfully.");
    } else if (operation === "backup") {
        flashStatus.textContent = "Backup completed";
        await showAlert("Backup eMMC completed successfully.");
    } else {
        flashStatus.textContent = "Flash completed";
        await showAlert("Flash Image completed successfully.");
    }
    refreshStorageInfo();
};

window.updateFlashProgress = (percent) => {
    const value = Math.max(0, Math.min(100, percent || 0));
    flashProgress.value = value;
    flashPercent = value;
    updateFlashStatusText();
};

window.appendLiveLog = (line) => {
    if (!line) {
        return;
    }
    liveLog.value += line + "\n";
    liveLog.scrollTop = liveLog.scrollHeight;
};

window.addEventListener("load", () => {
    setTimeout(() => {
        if (api && api.uiReady) {
            api.uiReady();
        }
    }, 0);
});

render();
