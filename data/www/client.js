export async function leftRetractDown() {
    await postCommand("l-ret");
}

export async function leftExtendDown() {
    await postCommand("l-ext");
}

export async function rightRetractDown() {
    await postCommand("r-ret");
}

export async function rightExtendDown() {
    await postCommand("r-ext");
}

export async function leftRetractUp() {
    await postCommand("l-0");
}

export async function leftExtendUp() {
    await postCommand("l-0");
}

export async function rightRetractUp() {
    await postCommand("r-0");
}

export async function rightExtendUp() {
    await postCommand("r-0");
}

function __commFail(context, err) {
    console.warn("[COMM FAIL]", context, err);

    if (window.handleCommError) {
        window.handleCommError({ context, err: String(err || "unknown") });
    } else {
        console.log("Verbindung instabil – versuche reconnect (kein Reload).");
    }
}

async function postCommand(command) {
    return new Promise((resolve, reject) => {
        $.ajax({
            url: "/command",
            method: "POST",
            data: { command },
            timeout: 6000
        })
        .done(() => resolve())
        .fail((xhr, status, error) => {
            __commFail(`POST /command (${command})`, error || status);
            reject(error || status);
        });
    });
}
