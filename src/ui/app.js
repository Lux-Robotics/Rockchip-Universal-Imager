let lastInfo = "";
let lastStatus = "disconnected";
let lastSoc = "";
let driverInstallRunning = false;
let flashRunning = false;
let selectedImagePath = "";
let logVisible = false;
let advancedVisible = false;
let logLoaded = false;
let logCleared = false;
let calculatingUsedSpace = false;
let quitPromptShowing = false;
let storageTargets = null;
const driverDeviceName = "Rockchip Bootloader Device";

const statusDot = document.getElementById("statusDot");
const statusText = document.getElementById("statusText");
const storageInfo = document.getElementById("storageInfo");
const storageSelector = document.getElementById("storageSelector");
const infoIcon = document.getElementById("infoIcon");
const driverStatus = document.getElementById("driverStatus");
const installDriver = document.getElementById("installDriver");
const flashStatus = document.getElementById("flashStatus");
const flashProgress = document.getElementById("flashProgress");
const connectDevice = document.getElementById("connectDevice");
const selectImage = document.getElementById("selectImage");
const flashImage = document.getElementById("flashImage");
const eraseEmmc = document.getElementById("eraseEmmc");
const secureEraseEmmc = document.getElementById("secureEraseEmmc");
const toggleAdvanced = document.getElementById("toggleAdvanced");
const advancedPanel = document.getElementById("advancedPanel");
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

function logAction(_message) {
    // Intentionally a no-op: UI actions (button clicks, panel toggles, etc.)
    // are no longer logged. The live-log panel mirrors the persistent log
    // file, which records only app + rkdeveloptool activity. Meaningful
    // context (backup destination, image path) still appears there via the
    // logged rkdeveloptool command line. Kept as a stub so its many call
    // sites don't each need removing.
}

function storageLabel(storage) {
    switch (Number(storage)) {
    case 1:
        return "eMMC";
    case 2:
        return "SD card";
    case 9:
        return "SPI NOR";
    default:
        return "storage";
    }
}

function storageBytesFromInfo(info) {
    if (!info) {
        return 0;
    }
    return Number(info.storage_bytes || info.emmc_bytes || 0);
}

function storageInfoBaseText(storage, storageBytes) {
    return storageLabel(storage) + ": " + Number(storageBytes || 0).toLocaleString() + " bytes";
}

function clearUsedSpaceSuffix() {
    if (!storageInfo.textContent) {
        return;
    }
    storageInfo.textContent = storageInfo.textContent.split("  ·  ")[0];
}

function availableStorageEntries(targets) {
    if (!targets || !targets.success) {
        return [];
    }
    const entries = [];
    if (targets.emmc_available) {
        entries.push({ value: 1, label: "eMMC" });
    }
    if (targets.sd_available) {
        entries.push({ value: 2, label: "SD card" });
    }
    if (targets.spinor_available) {
        entries.push({ value: 9, label: "SPI NOR" });
    }
    return entries;
}

function renderStorageSelector() {
    if (!storageSelector) {
        return;
    }

    const entries = availableStorageEntries(storageTargets);
    const enabledValues = new Set(entries.map((entry) => String(entry.value)));

    for (const option of storageSelector.options) {
        const available = enabledValues.has(option.value);
        option.disabled = !available;
        option.title = available ? "" : "not detected";
    }

    const selected = storageTargets && storageTargets.selected_storage
        ? String(storageTargets.selected_storage)
        : storageSelector.value;
    if (enabledValues.has(selected)) {
        storageSelector.value = selected;
    }
    const noStorageDevices = !!(storageTargets && !storageTargets.success);
    storageSelector.disabled = flashRunning || (lastStatus !== "connected" && !noStorageDevices);
}

async function refreshStorageTargets() {
    if (!api || !api.getStorageTargets) {
        return;
    }
    try {
        const targets = await api.getStorageTargets();
        storageTargets = targets || null;
    } catch (error) {
        storageTargets = null;
    }
    if (storageTargets && !storageTargets.success) {
        flashStatus.textContent = "no storage devices detected";
    } else if (flashStatus.textContent === "no storage devices detected") {
        flashStatus.textContent = "";
    }
    renderStorageSelector();
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
    const noStorageDevices = !!(storageTargets && !storageTargets.success);
    statusDot.style.background = connected ? "#2fa84f" : (detected ? "#2a6fd9" : (toolMissing ? "#d9822b" : "#a33"));
    const socSuffix = (lastSoc && (connected || detected)) ? " (" + lastSoc + ")" : "";
    statusText.textContent = toolMissing ? "rkdeveloptool not found" : ((detected ? "detected" : lastStatus) + socSuffix);
    flashImage.disabled = flashRunning || !connected || !selectedImagePath || noStorageDevices;
    eraseEmmc.disabled = flashRunning || !connected || noStorageDevices;
    secureEraseEmmc.disabled = flashRunning || !connected || noStorageDevices;
    backupEmmc.disabled = flashRunning || !connected || noStorageDevices;
    calculateUsed.disabled = flashRunning || !connected || calculatingUsedSpace || noStorageDevices;
    connectDevice.style.display = (connected || detected) ? "inline-block" : "none";
    connectDevice.textContent = connected ? "Disconnect" : "Connect";
    connectDevice.style.background = connected ? "#a33" : "#2a6fd9";
    connectDevice.disabled = flashRunning || noStorageDevices;
    cancelFlash.style.display = flashRunning ? "inline-block" : "none";
    if (toggleAdvanced) {
        toggleAdvanced.disabled = noStorageDevices;
    }
    if (toggleLog) {
        toggleLog.disabled = noStorageDevices;
    }
    if (copyLog) {
        copyLog.disabled = noStorageDevices;
    }
    if (clearLog) {
        clearLog.disabled = noStorageDevices;
    }
    if (installDriver) {
        installDriver.disabled = driverInstallRunning || noStorageDevices;
    }
    if (connected || detected) {
        const friendly = connected ? "device connected" : "device detected — press Connect";
        infoIcon.title = lastInfo.trim() || friendly;
    } else if (toolMissing) {
        infoIcon.title = "rkdeveloptool is missing from the app folder — reinstall or repair the app.";
        if (!driverInstallRunning) {
            driverStatus.textContent = "";
        }
    } else {
        infoIcon.title = "check usb";
        if (!driverInstallRunning) {
            driverStatus.textContent = "";
        }
    }
    if (storageSelector) {
        storageSelector.disabled = flashRunning || (lastStatus !== "connected" && !noStorageDevices);
        if (storageTargets && storageTargets.selected_storage) {
            storageSelector.value = String(storageTargets.selected_storage);
        }
    }
    if (noStorageDevices && !flashRunning) {
        flashStatus.textContent = "no storage devices detected";
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
    const selectedStorage = storageTargets && storageTargets.selected_storage
        ? storageTargets.selected_storage
        : (storageSelector ? Number(storageSelector.value) : 1);
    if (selectedStorage !== 1 || !storageTargets || !storageTargets.success || !storageTargets.emmc_available) {
        storageInfo.textContent = storageLabel(selectedStorage) + ": unknown";
        return;
    }
    const info = await api.getStorageInfo();
    if (!info.success) {
        storageInfo.textContent = storageLabel(selectedStorage) + ": unknown";
        return;
    }
    storageInfo.textContent = storageInfoBaseText(selectedStorage, storageBytesFromInfo(info));
}

window.updateDeviceStatus = (status) => {
    const changed = status !== lastStatus;
    lastStatus = status;
    render();
    if (changed && status === "connected") {
        refreshDriverInfo();
        setTimeout(() => {
            refreshStorageTargets().then(refreshStorageInfo);
        }, 0);
    }
};

window.updateDeviceInfo = (info) => {
    lastInfo = info || "";
    render();
};

window.updateDeviceSoc = (soc) => {
    lastSoc = soc || "";
    render();
};

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

toggleAdvanced.addEventListener("click", () => {
    advancedVisible = !advancedVisible;
    advancedPanel.style.display = advancedVisible ? "block" : "none";
    toggleAdvanced.textContent = advancedVisible ? "Hide Advanced Options" : "Show Advanced Options";
    logAction("Advanced options " + (advancedVisible ? "shown" : "hidden"));
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
    selectedImage.textContent = result.path + " (" + result.size_bytes.toLocaleString() + " bytes)";
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
    const disconnecting = lastStatus === "connected";
    logAction((disconnecting ? "Disconnect" : "Connect") + " clicked");
    if (!api || (disconnecting ? !api.disconnectDevice : !api.flashBootloader)) {
        flashStatus.textContent = (disconnecting ? "disconnect" : "connect") + " unavailable";
        return;
    }
    if (flashRunning) {
        return;
    }
    currentOperation = disconnecting ? "disconnect" : "connect";
    setFlashRunning(true);
    flashProgress.value = 0;
    startFlashAnimation(disconnecting ? "Disconnecting" : "Connecting");
    const result = disconnecting
        ? await api.disconnectDevice()
        : await api.flashBootloader();
    if (!result.started) {
        setFlashRunning(false);
        flashStatus.textContent = result.error || (disconnecting ? "disconnect failed" : "connect failed");
        logAction((disconnecting ? "Disconnect" : "Connect") + " failed to start: " + (result.error || "unknown error"));
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
        "This will erase the eMMC's partition table and flashed OS, leaving the device unbootable until " +
        "reflashed. Note: this is not a guaranteed secure wipe - depending on the device, old data may " +
        "still be physically recoverable afterward. Continue?"
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

secureEraseEmmc.addEventListener("click", async () => {
    logAction("Secure Erase clicked");
    if (!api || !api.secureEraseEmmc) {
        flashStatus.textContent = "secure erase unavailable";
        return;
    }
    if (flashRunning) {
        return;
    }
    const confirmed = await showConfirm(
        "This will overwrite the entire eMMC with zeros, physically destroying all data including the " +
        "flashed OS. This takes significantly longer than Quick Erase (potentially 15-60+ minutes " +
        "depending on device size and transfer speed) but is a real guarantee rather than relying on the " +
        "device's own erase command. This cannot be undone. Continue?"
    );
    logAction("Secure Erase " + (confirmed ? "confirmed" : "canceled by user"));
    if (!confirmed) {
        return;
    }
    currentOperation = "secure_erase";
    setFlashRunning(true);
    flashProgress.value = 0;
    startFlashAnimation("Secure erasing eMMC");
    const result = await api.secureEraseEmmc();
    if (!result.started) {
        setFlashRunning(false);
        flashStatus.textContent = result.error || "secure erase failed";
        logAction("Secure Erase failed to start: " + (result.error || "unknown error"));
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

    let result = await api.backupEmmc(picked.path, false);
    if (!result.started && result.needs_confirmation) {
        logAction("Backup size warning: " + result.message);
        const confirmed = await showConfirm(result.message);
        logAction("Backup size warning " + (confirmed ? "confirmed" : "declined"));
        if (!confirmed) {
            return;
        }
        result = await api.backupEmmc(picked.path, true);
    }

    if (!result.started) {
        flashStatus.textContent = result.message || "backup failed";
        logAction("Backup eMMC failed to start: " + (result.message || "unknown error"));
        return;
    }

    currentOperation = "backup";
    setFlashRunning(true);
    flashProgress.value = 0;
    startFlashAnimation("Backing up eMMC");
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
    const usedBytes = result.used_bytes.toLocaleString();
    storageInfo.textContent = baseText + "  ·  Used: " + usedBytes + " bytes";
    logAction("Calculated used space: " + usedBytes + " bytes");
    render();
});

if (storageSelector) {
    storageSelector.addEventListener("change", async () => {
        if (!api || !api.selectStorage) {
            return;
        }
        const nextStorage = Number(storageSelector.value);
        const previousStorage = storageTargets && storageTargets.selected_storage
            ? String(storageTargets.selected_storage)
            : storageSelector.value;
        const result = await api.selectStorage(nextStorage);
        if (!result.started) {
            storageSelector.value = previousStorage;
            flashStatus.textContent = result.error || "storage selection failed";
            return;
        }
        if (storageTargets) {
            storageTargets.selected_storage = nextStorage;
        }
        clearUsedSpaceSuffix();
        calculatingUsedSpace = false;
        await refreshStorageInfo();
        render();
    });
}

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
        : operation === "secure_erase" ? "Secure Erase"
        : operation === "connect" ? "Connect"
        : operation === "disconnect" ? "Disconnect"
        : operation === "backup" ? "Backup eMMC"
        : "Flash Image";

    if (result && result.cancelled) {
        flashProgress.value = 0;
        flashStatus.textContent = "Flash canceled";
        await showAlert(label + " was canceled.");
        return;
    }
    if (!result || !result.success) {
        flashProgress.value = 0;
        const error = (result && result.error) || "flash failed";
        flashStatus.textContent = error;
        await showAlert(label + " failed: " + error);
        return;
    }

    logAction((operation || "Flash") + " completed successfully");
    if (operation === "connect") {
        // The bootloader download is an internal step, not a user-facing
        // "flash" — the status dot turning green (once polling picks up
        // Loader mode) is the only feedback needed for a successful connect.
        flashStatus.textContent = "";
    } else if (operation === "disconnect") {
        flashStatus.textContent = "Disconnected";
    } else if (operation === "erase") {
        flashStatus.textContent = "Erase completed";
        await showAlert("Erase eMMC completed successfully.");
    } else if (operation === "secure_erase") {
        flashStatus.textContent = "Secure erase completed";
        await showAlert("Secure Erase completed successfully - the entire eMMC has been overwritten with zeros.");
    } else if (operation === "backup") {
        flashStatus.textContent = "Backup completed";
        await showAlert("Backup eMMC completed successfully.");
    } else {
        flashStatus.textContent = "Flash completed";
        await showAlert("Flash Image completed successfully.");
    }
    if (operation !== "disconnect" && lastStatus === "connected") {
        setTimeout(() => {
            refreshStorageTargets().then(refreshStorageInfo);
        }, 0);
    }
};

window.updateFlashProgress = (percent) => {
    const value = Math.max(0, Math.min(100, percent || 0));
    flashProgress.value = value;
    flashPercent = value;
    updateFlashStatusText();
};

window.onQuitDuringOperation = async () => {
    // The native window won't actually close on its own while a flash/erase/
    // backup is running (see window::event::close on the C++ side) - closing
    // mid-operation can leave the eMMC half-written or the device stuck
    // needing a reflash. Ask first; forceCloseWindow only fires if the user
    // confirms, and re-closing at that point is allowed through.
    if (quitPromptShowing) {
        return;
    }
    quitPromptShowing = true;
    try {
        const ok = await showConfirm(
            "A flash, erase, or backup is still in progress. Quitting now may leave the eMMC " +
            "partially written or the device needing to be reflashed. Quit anyway?"
        );
        if (ok && api && api.forceCloseWindow) {
            api.forceCloseWindow();
        }
    } finally {
        quitPromptShowing = false;
    }
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
