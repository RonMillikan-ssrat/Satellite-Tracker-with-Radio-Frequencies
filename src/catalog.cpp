#include "catalog.h"

const QList<SatelliteCatalog>& SatelliteCatalog::all() {
    static const QList<SatelliteCatalog> catalogs = {
        {"amateur", "Amateur radio",
         "CelesTrak amateur-radio group (the original default)",
         {"GROUP=amateur"}, {}},
        {"russian-weather", "Russian weather",
         "Meteor-M (137 MHz LRPT, receivable) and Elektro-L (GEO)",
         // Arktika-M 1 (47719) and 2 (58584) are in SATCAT but CelesTrak publishes
         // no GP elements for them (checked 2026-10). If that changes, add
         // "CATNR=47719", "CATNR=58584" here.
         {"NAME=METEOR-M", "NAME=ELEKTRO-L"}, {}},
        {"russia-china", "Russian & Chinese (all active)",
         "Every active satellite whose SATCAT owner is CIS (Russia/former USSR) or PRC (China)",
         {"GROUP=active"}, {"CIS", "PRC"}},
        {"starlink", "Constellation: Starlink",
         "SpaceX Starlink (Ku-band, not receivable with an RTL-SDR)",
         {"GROUP=starlink"}, {}},
        {"oneweb", "Constellation: OneWeb",
         "Eutelsat OneWeb", {"GROUP=oneweb"}, {}},
        {"kuiper", "Constellation: Kuiper",
         "Amazon Project Kuiper", {"GROUP=kuiper"}, {}},
        {"qianfan", "Constellation: Qianfan (China)",
         "Qianfan / Thousand Sails", {"GROUP=qianfan"}, {}},
        {"hulianwang", "Constellation: Guowang (China)",
         "China SatNet Guowang / Hulianwang", {"GROUP=hulianwang"}, {}},
    };
    return catalogs;
}

const SatelliteCatalog* SatelliteCatalog::find(const QString& id) {
    for (const SatelliteCatalog& c : all()) {
        if (c.id.compare(id, Qt::CaseInsensitive) == 0) return &c;
    }
    return nullptr;
}

QString SatelliteCatalog::gpUrl(const QString& query) {
    return "https://celestrak.org/NORAD/elements/gp.php?" + query + "&FORMAT=tle";
}

QString SatelliteCatalog::satcatUrl() {
    return "https://celestrak.org/satcat/records.php?GROUP=active&FORMAT=CSV";
}

QString SatelliteCatalog::satcatFallbackUrl() {
    return "https://celestrak.org/pub/satcat.csv";
}
