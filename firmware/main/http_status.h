// Read-only WiFi status page (GET /) with live updates pushed over
// server-sent events (GET /events). Intentionally has no write/control
// endpoints - USB serial remains the only way to change pin state
// (CLAUDE.md, "Protocol"); this only ever displays what the board is
// already doing.
#pragma once

// Starts the HTTP server on port 80 and the SSE broadcast task.
// Call once, after WiFi has an IP.
void http_status_start(void);
