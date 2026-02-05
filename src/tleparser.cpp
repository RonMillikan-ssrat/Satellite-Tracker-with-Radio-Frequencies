#include "tleparser.h"
#include <QStringList>

TLEParser::TLEParser() {}

QList<Satellite> TLEParser::parseTLEData(const QString& tleData) {
    QList<Satellite> satellites;
    QStringList lines = tleData.split('\n', Qt::SkipEmptyParts);
    
    // TLE format is 3 lines: name, line1, line2
    for (int i = 0; i + 2 < lines.size(); i += 3) {
        QString name = lines[i].trimmed();
        QString line1 = lines[i + 1].trimmed();
        QString line2 = lines[i + 2].trimmed();
        
        // Validate that these are actually TLE lines
        if (line1.startsWith("1 ") && line2.startsWith("2 ")) {
            if (validateTLELine(line1) && validateTLELine(line2)) {
                satellites.append(parseSingleTLE(name, line1, line2));
            }
        }
    }
    
    return satellites;
}

Satellite TLEParser::parseSingleTLE(const QString& name, 
                                     const QString& line1, 
                                     const QString& line2) {
    Satellite sat;
    sat.name = name;
    sat.line1 = line1;
    sat.line2 = line2;
    
    // Extract catalog number from line 1 (columns 3-7)
    if (line1.length() >= 7) {
        sat.catalogNumber = line1.mid(2, 5).trimmed().toInt();
    }
    
    return sat;
}

bool TLEParser::validateTLELine(const QString& line) {
    if (line.length() < 69) {
        return false;
    }
    
    // Get the checksum from the last character
    int providedChecksum = line.right(1).toInt();
    
    // Calculate checksum from first 68 characters
    int calculatedChecksum = calculateChecksum(line.left(68));
    
    return providedChecksum == calculatedChecksum;
}

int TLEParser::calculateChecksum(const QString& line) {
    int checksum = 0;
    
    for (QChar c : line) {
        if (c.isDigit()) {
            checksum += c.digitValue();
        } else if (c == '-') {
            checksum += 1;
        }
    }
    
    return checksum % 10;
}
