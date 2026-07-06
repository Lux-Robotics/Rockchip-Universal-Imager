let pollingEnabled = true;
let lastInfo = "";
let lastStatus = "disconnected";
let driverInstallRunning = false;
let flashRunning = false;
let selectedImagePath = "";
let logVisible = false;
let logLoaded = false;
let logCleared = false;
const driverDeviceName = "Rockchip Bootloader Device";

const statusDot = document.getElementById("statusDot");
const statusText = document.getElementById("statusText");
const deviceInfo = document.getElementById("deviceInfo");
const infoIcon = document.getElementById("infoIcon");
const pollingToggle = document.getElementById("pollingToggle");
const driverStatus = document.getElementById("driverStatus");
const installDriver = document.getElementById("installDriver");
const flashStatus = document.getElementById("flashStatus");
const flashProgress = document.getElementById("flashProgress");
const selectImage = document.getElementById("selectImage");
const flashBootloader = document.getElementById("flashBootloader");
const flashImage = document.getElementById("flashImage");
const selectedImage = document.getElementById("selectedImage");
const toggleLog = document.getElementById("toggleLog");
const logPanel = document.getElementById("logPanel");
const liveLog = document.getElementById("liveLog");
const copyLog = document.getElementById("copyLog");
const clearLog = document.getElementById("clearLog");
const api = window.saucer && window.saucer.exposed ? window.saucer.exposed : null;

function render() {
    const connected = lastStatus === "connected";
    const toolMissing = lastStatus === "tool_missing";
    statusDot.style.background = connected ? "#2fa84f" : (toolMissing ? "#d9822b" : "#a33");
    statusText.textContent = toolMissing ? "rkdeveloptool not found" : lastStatus;
    flashBootloader.disabled = flashRunning || !connected;
    flashImage.disabled = flashRunning || !connected;
    if (connected) {
        const text = lastInfo.trim() || "device connected";
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

function setFlashRunning(running) {
    flashRunning = running;
    selectImage.disabled = running;
    flashBootloader.disabled = running;
    flashImage.disabled = running;
    if (running) {
        flashStatus.textContent = "Flashing...";
    }
}

async function refreshDriverInfo() {
    if (!api || !api.getUsbDriverInfo) {
        driverStatus.textContent = "driver info unavailable";
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

window.updateDeviceStatus = (status) => {
    const changed = status !== lastStatus;
    lastStatus = status;
    render();
    if (changed && status === "connected") {
        refreshDriverInfo();
    }
};

window.updateDeviceInfo = (info) => {
    lastInfo = info || "";
    render();
};

pollingToggle.addEventListener("click", async () => {
    pollingEnabled = !pollingEnabled;
    pollingToggle.textContent = pollingEnabled ? "Pause Polling" : "Resume Polling";
    if (api && api.setPollingEnabled) {
        await api.setPollingEnabled(pollingEnabled);
    }
});

toggleLog.addEventListener("click", () => {
    logVisible = !logVisible;
    logPanel.style.display = logVisible ? "block" : "none";
    toggleLog.textContent = logVisible ? "Hide Log" : "Show Log";
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
});

document.getElementById("testLog").addEventListener("click", async () => {
    const message = "[hardware-helper] Test log message";
    if (api && api.logWrite) {
        await api.logWrite(message);
    }
    window.appendLiveLog(message);
});

selectImage.addEventListener("click", async () => {
    if (!api || !api.selectImageFile) {
        flashStatus.textContent = "file picker unavailable";
        return;
    }
    const result = await api.selectImageFile();
    if (!result.success) {
        flashStatus.textContent = result.error || "file picker canceled";
        return;
    }
    selectedImagePath = result.path;
    selectedImage.textContent = result.path;
    flashStatus.textContent = "image selected";
});

flashBootloader.addEventListener("click", async () => {
    if (!api || !api.flashBootloader) {
        flashStatus.textContent = "flash unavailable";
        return;
    }
    if (flashRunning) {
        return;
    }
    setFlashRunning(true);
    flashProgress.value = 0;
    const result = await api.flashBootloader();
    if (!result.started) {
        setFlashRunning(false);
        flashStatus.textContent = result.error || "flash failed";
    }
});

flashImage.addEventListener("click", async () => {
    if (!api || !api.flashImage) {
        flashStatus.textContent = "flash unavailable";
        return;
    }
    if (flashRunning) {
        return;
    }
    setFlashRunning(true);
    flashProgress.value = 0;
    const result = await api.flashImage(selectedImagePath);
    if (!result.started) {
        setFlashRunning(false);
        flashStatus.textContent = result.error || "flash failed";
    }
});

if (installDriver) {
    if (!api || !api.installUsbDriver) {
        installDriver.style.display = "none";
    } else {
        installDriver.addEventListener("click", async () => {
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
}

window.onDriverInstallComplete = (result) => {
    setDriverInstallRunning(false);
    if (!result || !result.success) {
        driverStatus.textContent = (result && result.error) || "driver install failed";
    } else {
        driverStatus.textContent = "driver installed";
    }
    refreshDriverInfo();
};

window.onFlashComplete = (result) => {
    setFlashRunning(false);
    if (!result || !result.success) {
        flashStatus.textContent = (result && result.error) || "flash failed";
    } else {
        flashStatus.textContent = "flash completed";
    }
};

window.updateFlashProgress = (percent) => {
    const value = Math.max(0, Math.min(100, percent || 0));
    flashProgress.value = value;
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
