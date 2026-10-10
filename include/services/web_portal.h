#pragma once

/**
 * The settings site: one page plus a small JSON API, served both on the setup
 * access point (as a captive portal) and on the home network.
 */
namespace web {

/**
 * Start serving. `captive` adds the catch-all DNS used on the setup AP.
 *
 * Requests are handled on the server's own task: sending a page to a browser
 * over a poor link can block for seconds, and that must not stall the radar.
 */
void start(bool captive);
/** Stop serving; waits for a request in progress to finish. */
void stop();

}  // namespace web
