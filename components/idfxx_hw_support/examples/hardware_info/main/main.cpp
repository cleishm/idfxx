// SPDX-License-Identifier: Apache-2.0

#include <idfxx/chip>
#include <idfxx/log>
#include <idfxx/mac>

static constexpr idfxx::log::logger logger{"example"};

extern "C" void app_main() {
    // --- Chip Information ---
    logger.info("=== Chip Information ===");

    auto info = idfxx::chip_info::get();
    logger.info("Model:    {}", info.model());
    logger.info("Cores:    {}", info.cores());
    logger.info("Revision: {}.{}", info.major_revision(), info.minor_revision());

    auto features = info.features();
    logger.info("WiFi:           {}", features.contains(idfxx::chip_feature::wifi));
    logger.info("BLE:            {}", features.contains(idfxx::chip_feature::ble));
    logger.info("BT Classic:     {}", features.contains(idfxx::chip_feature::bt_classic));
    logger.info("IEEE 802.15.4:  {}", features.contains(idfxx::chip_feature::ieee802154));
    logger.info("Embedded flash: {}", features.contains(idfxx::chip_feature::embedded_flash));
    logger.info("Embedded PSRAM: {}", features.contains(idfxx::chip_feature::embedded_psram));

    // --- MAC Addresses ---
    logger.info("=== MAC Addresses ===");

    auto base = idfxx::base_mac_address();
    logger.info("Base MAC:     {}", base);

    auto wifi_sta = idfxx::read_mac(idfxx::mac_type::wifi_sta);
    logger.info("WiFi STA MAC: {}", wifi_sta);

    auto wifi_ap = idfxx::read_mac(idfxx::mac_type::wifi_softap);
    logger.info("WiFi AP MAC:  {}", wifi_ap);

    auto bt = idfxx::read_mac(idfxx::mac_type::bt);
    logger.info("BT MAC:       {}", bt);

    auto eth = idfxx::read_mac(idfxx::mac_type::ethernet);
    logger.info("Ethernet MAC: {}", eth);

    // Access individual bytes of a MAC address
    logger.info(
        "Base MAC bytes: {:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}",
        base.bytes()[0],
        base.bytes()[1],
        base.bytes()[2],
        base.bytes()[3],
        base.bytes()[4],
        base.bytes()[5]
    );

    logger.info("Done!");
}
