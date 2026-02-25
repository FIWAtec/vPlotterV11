#pragma once

// Runtime helper for /commands on SD. Does not block webserver startup.
bool sdCommandsEnsureMounted();
bool sdCommandsIsMounted();
