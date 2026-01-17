#pragma once

#include "Types.h"

/** @brief Константы разметки системной памяти на ленте */
namespace MemoryLayout {

// Позиции на ленте
inline constexpr long long kMemBegin     = -10;  // BOM (начало памяти)
inline constexpr long long kMemEnd       = -1;   // EOM (конец памяти)
inline constexpr long long kMSBPosition  = -9;   // Старший бит (bit 0)
inline constexpr long long kLSBPosition  = -2;   // Младший бит (bit 7)
inline constexpr long long kUserZoneStart = 0;   // Начало пользовательской зоны
inline constexpr int kMemBits = 8;               // Бит в переменной

// Системные символы
inline const Symbol kSymBOM    = "BOM";   // Маркер начала памяти
inline const Symbol kSymEOM    = "EOM";   // Маркер конца памяти
inline const Symbol kBit0      = "0_";    // Бит 0
inline const Symbol kBit1      = "1_";    // Бит 1
inline const Symbol kPosMarker = "#";     // Маркер позиции

/** @brief Позиция бита на ленте (0=MSB, 7=LSB) */
inline constexpr long long bitPosition(int bitIndex) {
    return kMSBPosition + bitIndex;
}

/** @brief Шагов вправо от BOM до бита */
inline constexpr int stepsFromBOMToBit(int bitIndex) {
    return bitIndex + 1;
}

/** @brief Шагов влево от EOM до бита */
inline constexpr int stepsFromEOMToBit(int bitIndex) {
    return kMemBits - bitIndex;
}

} // namespace MemoryLayout
