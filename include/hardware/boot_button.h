#pragma once

/**
 * BOOT button (active LOW). Releases are classified in an interrupt, so a
 * press is not lost while the main loop is busy drawing or connecting:
 *
 *   short tap                         -> consumeTap()
 *   held past kBootInfoHoldMs         -> consumeInfoPress()
 *   held past kBootSetupHoldMs        -> neither; poll bootButtonHeldMs()
 */
void bootButtonInit();
bool bootButtonPressed();
bool bootButtonConsumeTap();
bool bootButtonConsumeInfoPress();
/** How long the button has been down (ms); 0 while it is up. */
unsigned long bootButtonHeldMs();
