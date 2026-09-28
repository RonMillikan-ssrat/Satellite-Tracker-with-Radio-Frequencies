#ifndef NUMERICTABLEITEM_H
#define NUMERICTABLEITEM_H

#include <QTableWidgetItem>

// Custom table item that sorts by numeric value instead of text
class NumericTableItem : public QTableWidgetItem {
public:
    NumericTableItem(double value, const QString& displayText)
        : QTableWidgetItem(displayText), m_numericValue(value) {}
    
    bool operator<(const QTableWidgetItem& other) const override {
        const NumericTableItem* numericOther = dynamic_cast<const NumericTableItem*>(&other);
        if (numericOther) {
            return m_numericValue < numericOther->m_numericValue;
        }
        return QTableWidgetItem::operator<(other);
    }
    
private:
    double m_numericValue;
};

#endif // NUMERICTABLEITEM_H
