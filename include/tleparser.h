#ifndef TLEPARSER_H
#define TLEPARSER_H

#include <QString>
#include <QList>
#include "satellite.h"

class TLEParser {
public:
    TLEParser();
    
    // Parse TLE data from string (3-line format: name, line1, line2)
    static QList<Satellite> parseTLEData(const QString& tleData);
    
    // Parse a single satellite from 3 lines
    static Satellite parseSingleTLE(const QString& name, 
                                    const QString& line1, 
                                    const QString& line2);
    
    // Validate TLE line checksums
    static bool validateTLELine(const QString& line);
    
private:
    static int calculateChecksum(const QString& line);
};

#endif // TLEPARSER_H
