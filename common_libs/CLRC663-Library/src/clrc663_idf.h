#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"

// ── Handle ────────────────────────────────────────────────────────────────────
typedef struct {
    spi_device_handle_t spi;
    gpio_num_t          cs_pin;
    uint8_t             uid_lsb[8];   // UID stored LSB-first for TX frames
} clrc663_t;

// ── Lifecycle ─────────────────────────────────────────────────────────────────

/**
 * Add the CLRC663 as a device on an already-initialised SPI bus.
 * Call spi_bus_initialize() before this.
 *
 * @param host   SPI host (e.g. SPI2_HOST)
 * @param cs_pin GPIO number wired to the chip-select pin
 * @param dev    Output handle – pass this to every other function
 */
esp_err_t clrc663_init(spi_host_device_t host, gpio_num_t cs_pin, clrc663_t *dev);

/** Software-reset the chip and wait for it to come back up. */
void clrc663_reset(clrc663_t *dev);

// ── Raw register access (useful for debugging) ────────────────────────────────
uint8_t  clrc663_read_reg (clrc663_t *dev, uint8_t reg);
void     clrc663_write_reg(clrc663_t *dev, uint8_t reg, uint8_t value);

// ── ISO 15693 protocol setup ──────────────────────────────────────────────────

/**
 * Load the ISO 15693 protocol into the chip and apply the NXP AN1102
 * recommended register values (1-of-4, SSC 26 kbit/s, single sub-carrier).
 * Call once after clrc663_reset().
 */
void clrc663_iso15693_init(clrc663_t *dev);

// ── ISO 15693 tag operations ──────────────────────────────────────────────────

/**
 * Read the 8-byte hardware UID of the tag in the field.
 *
 * @param dev  Reader handle
 * @param uid  Output buffer – must be at least 8 bytes
 * @return     Number of UID bytes received (8 on success, 0 on failure)
 */
uint8_t clrc663_iso15693_read_uid(clrc663_t *dev, uint8_t *uid);

/**
 * Read one 4-byte native block from an ISO 15693 tag.
 *
 * @param dev        Reader handle
 * @param uid        8-byte UID (display order, from read_uid)
 * @param block_num  Native block index
 * @param data       Output buffer – must be at least 4 bytes
 * @return           true on success, false on timeout / error
 */
bool clrc663_iso15693_read_block(clrc663_t *dev, const uint8_t *uid,
                                 uint8_t block_num, uint8_t *data);

/**
 * Read two consecutive 4-byte blocks and combine into one 8-byte buffer.
 * Use this to recover the 8-char ASCII strings written by the IFM / PLC:
 *   string 0 -> first_block=0  (blocks 0+1)
 *   string 1 -> first_block=2  (blocks 2+3)
 *   string 2 -> first_block=4  (blocks 4+5)
 *
 * @param dev         Reader handle
 * @param uid         8-byte UID (display order, from read_uid)
 * @param first_block First of the two consecutive blocks (must be even)
 * @param data        Output buffer – must be at least 9 bytes (8 data + NUL)
 * @return            true if both blocks read OK, false if either fails
 */
bool clrc663_iso15693_read_block_pair(clrc663_t *dev, const uint8_t *uid,
                                      uint8_t first_block, uint8_t *data);

/**
 * Write one 4-byte native block to an ISO 15693 tag.
 *
 * @param dev        Reader handle
 * @param uid        8-byte UID (display order, from read_uid)
 * @param block_num  Native block index
 * @param data       4 bytes to write
 * @return           true on success, false on timeout / error / tag NACK
 */
bool clrc663_iso15693_write_block(clrc663_t *dev, const uint8_t *uid,
                                  uint8_t block_num, const uint8_t *data);

/**
 * Write an 8-char ASCII string across two consecutive 4-byte blocks.
 *   string 0 -> first_block=0  (blocks 0+1)
 *   string 1 -> first_block=2  (blocks 2+3)
 *   string 2 -> first_block=4  (blocks 4+5)
 *
 * @param dev         Reader handle
 * @param uid         8-byte UID (display order, from read_uid)
 * @param first_block First of the two consecutive blocks (must be even)
 * @param data        8 bytes to write
 * @return            true if both blocks written OK
 */
bool clrc663_iso15693_write_block_pair(clrc663_t *dev, const uint8_t *uid,
                                       uint8_t first_block, const uint8_t *data);

/**
 * Configure LPCD (Low-Power Card Detection) and trigger the LPCD command.
 * The chip will autonomously pulse the RF field every ~1 second and assert
 * the IRQ pin when a card is detected, allowing the host to wake from sleep.
 *
 * Call this just before entering deep sleep. Re-run clrc663_iso15693_init()
 * after waking before attempting any tag reads.
 */
void setup_lpcd(clrc663_t *dev);
