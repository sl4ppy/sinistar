// Motorola 6809 CPU Emulator
// Cycle-accurate implementation for Williams arcade hardware
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>

// Condition code register bits
enum CC_Bits {
    CC_C = 0x01, // Carry
    CC_V = 0x02, // Overflow
    CC_Z = 0x04, // Zero
    CC_N = 0x08, // Negative
    CC_I = 0x10, // IRQ mask
    CC_H = 0x20, // Half carry
    CC_F = 0x40, // FIRQ mask
    CC_E = 0x80, // Entire flag
};

struct CPU6809 {
    // Registers
    uint16_t PC;  // Program counter
    uint16_t S;   // Hardware stack pointer
    uint16_t U;   // User stack pointer
    uint16_t X;   // Index register X
    uint16_t Y;   // Index register Y
    uint8_t  A;   // Accumulator A
    uint8_t  B;   // Accumulator B
    uint8_t  DP;  // Direct page register
    uint8_t  CC;  // Condition code register

    // Combined D register (A:B)
    uint16_t getD() const { return ((uint16_t)A << 8) | B; }
    void setD(uint16_t v) { A = (uint8_t)(v >> 8); B = (uint8_t)(v & 0xFF); }

    // State
    int cycles;        // Cycle counter for current instruction
    int totalCycles;   // Total cycles executed
    bool nmiPending;
    bool irqPending;
    bool firqPending;
    bool nmiArmed;     // NMI needs to be armed on Williams hardware
    bool halted;
    bool waiting;      // CWAI/SYNC

    // Memory read/write callbacks
    uint8_t (*memRead)(uint16_t addr, void* ctx);
    void (*memWrite)(uint16_t addr, uint8_t val, void* ctx);
    void* memCtx;

    void init() {
        A = B = 0;
        X = Y = S = U = 0;
        DP = 0;
        CC = CC_I | CC_F; // Interrupts masked at reset
        cycles = 0;
        totalCycles = 0;
        nmiPending = false;
        irqPending = false;
        firqPending = false;
        nmiArmed = false;
        halted = false;
        waiting = false;
    }

    void reset() {
        init();
        CC = CC_I | CC_F;
        DP = 0;
        // Read reset vector from $FFFE-$FFFF
        uint8_t hi = memRead(0xFFFE, memCtx);
        uint8_t lo = memRead(0xFFFF, memCtx);
        PC = ((uint16_t)hi << 8) | lo;
        cycles = 0;
    }

    // Memory access helpers
    uint8_t read8(uint16_t addr) {
        return memRead(addr, memCtx);
    }

    void write8(uint16_t addr, uint8_t val) {
        memWrite(addr, val, memCtx);
    }

    uint16_t read16(uint16_t addr) {
        uint8_t hi = read8(addr);
        uint8_t lo = read8(addr + 1);
        return ((uint16_t)hi << 8) | lo;
    }

    void write16(uint16_t addr, uint16_t val) {
        write8(addr, (uint8_t)(val >> 8));
        write8(addr + 1, (uint8_t)(val & 0xFF));
    }

    uint8_t fetch8() {
        uint8_t v = read8(PC);
        PC++;
        return v;
    }

    uint16_t fetch16() {
        uint16_t v = read16(PC);
        PC += 2;
        return v;
    }

    // Push/pull on S stack
    void pushS8(uint8_t val) { S--; write8(S, val); }
    void pushS16(uint16_t val) { S -= 2; write16(S, val); }
    uint8_t pullS8() { uint8_t v = read8(S); S++; return v; }
    uint16_t pullS16() { uint16_t v = read16(S); S += 2; return v; }

    // Push/pull on U stack
    void pushU8(uint8_t val) { U--; write8(U, val); }
    void pushU16(uint16_t val) { U -= 2; write16(U, val); }
    uint8_t pullU8() { uint8_t v = read8(U); U++; return v; }
    uint16_t pullU16() { uint16_t v = read16(U); U += 2; return v; }

    // Flag helpers
    void setCC(uint8_t mask, bool val) {
        if (val) CC |= mask; else CC &= ~mask;
    }

    bool getCC(uint8_t mask) const { return (CC & mask) != 0; }

    void setNZ8(uint8_t val) {
        setCC(CC_N, val & 0x80);
        setCC(CC_Z, val == 0);
    }

    void setNZ16(uint16_t val) {
        setCC(CC_N, val & 0x8000);
        setCC(CC_Z, val == 0);
    }

    // Addressing modes - return effective address
    uint16_t addrDirect() {
        uint8_t off = fetch8();
        return ((uint16_t)DP << 8) | off;
    }

    uint16_t addrExtended() {
        return fetch16();
    }

    uint16_t addrIndexed() {
        uint8_t postbyte = fetch8();
        uint16_t* reg;
        switch ((postbyte >> 5) & 0x03) {
            case 0: reg = &X; break;
            case 1: reg = &Y; break;
            case 2: reg = &S; break;
            case 3: reg = &U; break;
            default: reg = &X; break;
        }

        uint16_t ea = 0;
        bool indirect = false;

        if (!(postbyte & 0x80)) {
            // 5-bit signed offset
            int8_t off = (int8_t)(postbyte & 0x1F);
            if (off & 0x10) off |= 0xE0; // sign extend
            ea = *reg + (int16_t)off;
            cycles += 1;
        } else {
            indirect = (postbyte & 0x10) != 0;
            switch (postbyte & 0x0F) {
                case 0x00: // ,R+
                    ea = *reg; (*reg)++;
                    cycles += 2;
                    break;
                case 0x01: // ,R++
                    ea = *reg; (*reg) += 2;
                    cycles += 3;
                    break;
                case 0x02: // ,-R
                    (*reg)--; ea = *reg;
                    cycles += 2;
                    break;
                case 0x03: // ,--R
                    (*reg) -= 2; ea = *reg;
                    cycles += 3;
                    break;
                case 0x04: // ,R (no offset)
                    ea = *reg;
                    cycles += 0;
                    break;
                case 0x05: // B,R
                    ea = *reg + (int16_t)(int8_t)B;
                    cycles += 1;
                    break;
                case 0x06: // A,R
                    ea = *reg + (int16_t)(int8_t)A;
                    cycles += 1;
                    break;
                case 0x08: // n8,R
                    ea = *reg + (int16_t)(int8_t)fetch8();
                    cycles += 1;
                    break;
                case 0x09: // n16,R
                    ea = *reg + (int16_t)fetch16();
                    cycles += 4;
                    break;
                case 0x0B: // D,R
                    ea = *reg + getD();
                    cycles += 4;
                    break;
                case 0x0C: // n8,PCR
                    { int8_t off = (int8_t)fetch8();
                      ea = PC + (int16_t)off;
                      cycles += 1; }
                    break;
                case 0x0D: // n16,PCR
                    { int16_t off = (int16_t)fetch16();
                      ea = PC + off;
                      cycles += 5; }
                    break;
                case 0x0F: // [n16] (extended indirect, only with indirect bit)
                    ea = fetch16();
                    cycles += 2;
                    break;
                default:
                    // Undefined, treat as ,R
                    ea = *reg;
                    break;
            }
            if (indirect) {
                ea = read16(ea);
                cycles += 2;
            }
        }
        return ea;
    }

    // ALU operations - 8-bit
    uint8_t op_add8(uint8_t a, uint8_t b, bool withCarry = false) {
        uint8_t c = (withCarry && getCC(CC_C)) ? 1 : 0;
        uint16_t r = (uint16_t)a + b + c;
        setCC(CC_H, ((a ^ b ^ (uint8_t)r) & 0x10) != 0);
        setNZ8((uint8_t)r);
        setCC(CC_C, r > 0xFF);
        setCC(CC_V, ((a ^ b ^ 0x80) & (a ^ (uint8_t)r) & 0x80) != 0);
        return (uint8_t)r;
    }

    uint8_t op_sub8(uint8_t a, uint8_t b, bool withCarry = false) {
        uint8_t c = (withCarry && getCC(CC_C)) ? 1 : 0;
        uint16_t r = (uint16_t)a - b - c;
        setNZ8((uint8_t)r);
        setCC(CC_C, r > 0xFF);
        setCC(CC_V, ((a ^ b) & (a ^ (uint8_t)r) & 0x80) != 0);
        return (uint8_t)r;
    }

    // ALU - 16-bit
    uint16_t op_add16(uint16_t a, uint16_t b) {
        uint32_t r = (uint32_t)a + b;
        setNZ16((uint16_t)r);
        setCC(CC_C, r > 0xFFFF);
        setCC(CC_V, ((a ^ b ^ 0x8000) & (a ^ (uint16_t)r) & 0x8000) != 0);
        return (uint16_t)r;
    }

    uint16_t op_sub16(uint16_t a, uint16_t b) {
        uint32_t r = (uint32_t)a - b;
        setNZ16((uint16_t)r);
        setCC(CC_C, r > 0xFFFF);
        setCC(CC_V, ((a ^ b) & (a ^ (uint16_t)r) & 0x8000) != 0);
        return (uint16_t)r;
    }

    // DAA
    void op_daa() {
        uint16_t correction = 0;
        uint8_t lsn = A & 0x0F;
        uint8_t msn = A & 0xF0;
        if (getCC(CC_H) || lsn > 9) correction |= 0x06;
        if (getCC(CC_C) || msn > 0x90 || (msn > 0x80 && lsn > 9)) {
            correction |= 0x60;
        }
        uint16_t r = (uint16_t)A + correction;
        setNZ8((uint8_t)r);
        if (r & 0x100) setCC(CC_C, true);
        A = (uint8_t)r;
    }

    // Transfer/exchange register helpers
    uint16_t getRegValue(int code) {
        switch (code) {
            case 0x0: return getD();
            case 0x1: return X;
            case 0x2: return Y;
            case 0x3: return U;
            case 0x4: return S;
            case 0x5: return PC;
            case 0x8: return A;
            case 0x9: return B;
            case 0xA: return CC;
            case 0xB: return DP;
            default: return 0xFF;
        }
    }

    void setRegValue(int code, uint16_t val) {
        switch (code) {
            case 0x0: setD(val); break;
            case 0x1: X = val; break;
            case 0x2: Y = val; break;
            case 0x3: U = val; break;
            case 0x4: S = val; break;
            case 0x5: PC = val; break;
            case 0x8: A = (uint8_t)val; break;
            case 0x9: B = (uint8_t)val; break;
            case 0xA: CC = (uint8_t)val; break;
            case 0xB: DP = (uint8_t)val; break;
        }
    }

    // Push/pull register lists
    int pushRegs(uint8_t postbyte, bool toS) {
        int count = 0;
        if (postbyte & 0x80) { if (toS) pushS16(PC); else pushU16(PC); count += 2; }
        if (postbyte & 0x40) { if (toS) pushS16(toS ? U : S); else pushU16(toS ? U : S); count += 2; }
        if (postbyte & 0x20) { if (toS) pushS16(Y); else pushU16(Y); count += 2; }
        if (postbyte & 0x10) { if (toS) pushS16(X); else pushU16(X); count += 2; }
        if (postbyte & 0x08) { if (toS) pushS8(DP); else pushU8(DP); count += 1; }
        if (postbyte & 0x04) { if (toS) pushS8(B); else pushU8(B); count += 1; }
        if (postbyte & 0x02) { if (toS) pushS8(A); else pushU8(A); count += 1; }
        if (postbyte & 0x01) { if (toS) pushS8(CC); else pushU8(CC); count += 1; }
        return count;
    }

    int pullRegs(uint8_t postbyte, bool fromS) {
        int count = 0;
        if (postbyte & 0x01) { CC = fromS ? pullS8() : pullU8(); count += 1; }
        if (postbyte & 0x02) { A = fromS ? pullS8() : pullU8(); count += 1; }
        if (postbyte & 0x04) { B = fromS ? pullS8() : pullU8(); count += 1; }
        if (postbyte & 0x08) { DP = fromS ? pullS8() : pullU8(); count += 1; }
        if (postbyte & 0x10) { X = fromS ? pullS16() : pullU16(); count += 2; }
        if (postbyte & 0x20) { Y = fromS ? pullS16() : pullU16(); count += 2; }
        if (postbyte & 0x40) { if (fromS) U = pullS16(); else S = pullU16(); count += 2; }
        if (postbyte & 0x80) { PC = fromS ? pullS16() : pullU16(); count += 2; }
        return count;
    }

    // Interrupt handling
    void doNMI() {
        CC |= CC_E;
        pushRegs(0xFF, true); // push all
        CC |= CC_I | CC_F;
        PC = read16(0xFFFC);
        cycles += 19;
        nmiPending = false;
    }

    void doFIRQ() {
        CC &= ~CC_E;
        pushRegs(0x81, true); // push CC, PC only
        CC |= CC_I | CC_F;
        PC = read16(0xFFF6);
        cycles += 10;
        firqPending = false;
    }

    void doIRQ() {
        CC |= CC_E;
        pushRegs(0xFF, true); // push all
        CC |= CC_I;
        PC = read16(0xFFF8);
        cycles += 19;
        irqPending = false;
    }

    // Execute one instruction, return cycles consumed
    int execute() {
        // Check interrupts
        if (nmiPending && nmiArmed) {
            if (waiting) { waiting = false; }
            doNMI();
            return cycles;
        }
        if (firqPending && !(CC & CC_F)) {
            if (waiting) { waiting = false; }
            doFIRQ();
            return cycles;
        }
        if (irqPending && !(CC & CC_I)) {
            if (waiting) { waiting = false; }
            doIRQ();
            return cycles;
        }

        if (waiting || halted) {
            cycles = 1;
            totalCycles += 1;
            return 1;
        }

        cycles = 0;
        uint8_t op = fetch8();

        switch (op) {
            // Page 2 prefix
            case 0x10: return executePage2();
            // Page 3 prefix
            case 0x11: return executePage3();

            // NEG direct
            case 0x00: { uint16_t ea = addrDirect(); uint8_t v = read8(ea); uint8_t r = op_sub8(0, v);
                         write8(ea, r); cycles += 6; break; }
            // COM direct
            case 0x03: { uint16_t ea = addrDirect(); uint8_t v = ~read8(ea);
                         write8(ea, v); setNZ8(v); setCC(CC_C, true); setCC(CC_V, false); cycles += 6; break; }
            // LSR direct
            case 0x04: { uint16_t ea = addrDirect(); uint8_t v = read8(ea);
                         setCC(CC_C, v & 1); v >>= 1; setNZ8(v); write8(ea, v); cycles += 6; break; }
            // ROR direct
            case 0x06: { uint16_t ea = addrDirect(); uint8_t v = read8(ea); bool c = getCC(CC_C);
                         setCC(CC_C, v & 1); v = (v >> 1) | (c ? 0x80 : 0); setNZ8(v); write8(ea, v); cycles += 6; break; }
            // ASR direct
            case 0x07: { uint16_t ea = addrDirect(); uint8_t v = read8(ea);
                         setCC(CC_C, v & 1); v = (v >> 1) | (v & 0x80); setNZ8(v); write8(ea, v); cycles += 6; break; }
            // ASL/LSL direct
            case 0x08: { uint16_t ea = addrDirect(); uint8_t v = read8(ea);
                         setCC(CC_C, v & 0x80); uint8_t r = v << 1;
                         setNZ8(r); setCC(CC_V, ((v ^ r) & 0x80) != 0); write8(ea, r); cycles += 6; break; }
            // ROL direct
            case 0x09: { uint16_t ea = addrDirect(); uint8_t v = read8(ea); bool c = getCC(CC_C);
                         setCC(CC_C, v & 0x80); uint8_t r = (v << 1) | (c ? 1 : 0);
                         setNZ8(r); setCC(CC_V, ((v ^ r) & 0x80) != 0); write8(ea, r); cycles += 6; break; }
            // DEC direct
            case 0x0A: { uint16_t ea = addrDirect(); uint8_t v = read8(ea);
                         setCC(CC_V, v == 0x80); v--; setNZ8(v); write8(ea, v); cycles += 6; break; }
            // INC direct
            case 0x0C: { uint16_t ea = addrDirect(); uint8_t v = read8(ea);
                         setCC(CC_V, v == 0x7F); v++; setNZ8(v); write8(ea, v); cycles += 6; break; }
            // TST direct
            case 0x0D: { uint16_t ea = addrDirect(); uint8_t v = read8(ea);
                         setNZ8(v); setCC(CC_V, false); cycles += 6; break; }
            // JMP direct
            case 0x0E: { PC = addrDirect(); cycles += 3; break; }
            // CLR direct
            case 0x0F: { uint16_t ea = addrDirect(); write8(ea, 0);
                         setCC(CC_N, false); setCC(CC_Z, true); setCC(CC_V, false); setCC(CC_C, false);
                         cycles += 6; break; }

            // NOP
            case 0x12: cycles += 2; break;
            // SYNC
            case 0x13: waiting = true; cycles += 2; break;
            // LBRA
            case 0x16: { int16_t off = (int16_t)fetch16(); PC += off; cycles += 5; break; }
            // LBSR
            case 0x17: { int16_t off = (int16_t)fetch16(); pushS16(PC); PC += off; cycles += 9; break; }
            // DAA
            case 0x19: op_daa(); cycles += 2; break;
            // ORCC
            case 0x1A: CC |= fetch8(); cycles += 3; break;
            // ANDCC
            case 0x1C: CC &= fetch8(); cycles += 3; break;
            // SEX
            case 0x1D: A = (B & 0x80) ? 0xFF : 0x00; setNZ16(getD()); cycles += 2; break;
            // EXG
            case 0x1E: { uint8_t pb = fetch8(); int r1 = (pb >> 4) & 0x0F; int r2 = pb & 0x0F;
                         uint16_t t = getRegValue(r1); setRegValue(r1, getRegValue(r2)); setRegValue(r2, t);
                         cycles += 8; break; }
            // TFR
            case 0x1F: { uint8_t pb = fetch8(); int r1 = (pb >> 4) & 0x0F; int r2 = pb & 0x0F;
                         setRegValue(r2, getRegValue(r1)); cycles += 6; break; }

            // BRA
            case 0x20: { int8_t off = (int8_t)fetch8(); PC += off; cycles += 3; break; }
            // BRN
            case 0x21: fetch8(); cycles += 3; break;
            // BHI
            case 0x22: { int8_t off = (int8_t)fetch8(); if (!(CC & (CC_C|CC_Z))) PC += off; cycles += 3; break; }
            // BLS
            case 0x23: { int8_t off = (int8_t)fetch8(); if (CC & (CC_C|CC_Z)) PC += off; cycles += 3; break; }
            // BCC/BHS
            case 0x24: { int8_t off = (int8_t)fetch8(); if (!(CC & CC_C)) PC += off; cycles += 3; break; }
            // BCS/BLO
            case 0x25: { int8_t off = (int8_t)fetch8(); if (CC & CC_C) PC += off; cycles += 3; break; }
            // BNE
            case 0x26: { int8_t off = (int8_t)fetch8(); if (!(CC & CC_Z)) PC += off; cycles += 3; break; }
            // BEQ
            case 0x27: { int8_t off = (int8_t)fetch8(); if (CC & CC_Z) PC += off; cycles += 3; break; }
            // BVC
            case 0x28: { int8_t off = (int8_t)fetch8(); if (!(CC & CC_V)) PC += off; cycles += 3; break; }
            // BVS
            case 0x29: { int8_t off = (int8_t)fetch8(); if (CC & CC_V) PC += off; cycles += 3; break; }
            // BPL
            case 0x2A: { int8_t off = (int8_t)fetch8(); if (!(CC & CC_N)) PC += off; cycles += 3; break; }
            // BMI
            case 0x2B: { int8_t off = (int8_t)fetch8(); if (CC & CC_N) PC += off; cycles += 3; break; }
            // BGE
            case 0x2C: { int8_t off = (int8_t)fetch8();
                         bool n = getCC(CC_N), v = getCC(CC_V);
                         if (n == v) { PC += off; } cycles += 3; break; }
            // BLT
            case 0x2D: { int8_t off = (int8_t)fetch8();
                         bool n = getCC(CC_N), v = getCC(CC_V);
                         if (n != v) { PC += off; } cycles += 3; break; }
            // BGT
            case 0x2E: { int8_t off = (int8_t)fetch8();
                         bool n = getCC(CC_N), v = getCC(CC_V);
                         if (!getCC(CC_Z) && n == v) { PC += off; } cycles += 3; break; }
            // BLE
            case 0x2F: { int8_t off = (int8_t)fetch8();
                         bool n = getCC(CC_N), v = getCC(CC_V);
                         if (getCC(CC_Z) || n != v) { PC += off; } cycles += 3; break; }

            // LEAX
            case 0x30: X = addrIndexed(); setCC(CC_Z, X == 0); cycles += 4; break;
            // LEAY
            case 0x31: Y = addrIndexed(); setCC(CC_Z, Y == 0); cycles += 4; break;
            // LEAS
            case 0x32: S = addrIndexed(); cycles += 4; break;
            // LEAU
            case 0x33: U = addrIndexed(); cycles += 4; break;
            // PSHS
            case 0x34: { uint8_t pb = fetch8(); cycles += 5 + pushRegs(pb, true); break; }
            // PULS
            case 0x35: { uint8_t pb = fetch8(); cycles += 5 + pullRegs(pb, true); break; }
            // PSHU
            case 0x36: { uint8_t pb = fetch8(); cycles += 5 + pushRegs(pb, false); break; }
            // PULU
            case 0x37: { uint8_t pb = fetch8(); cycles += 5 + pullRegs(pb, false); break; }
            // RTS
            case 0x39: PC = pullS16(); cycles += 5; break;
            // ABX
            case 0x3A: X += B; cycles += 3; break;
            // RTI
            case 0x3B: {
                CC = pullS8();
                if (CC & CC_E) {
                    pullRegs(0xFE, true); // all except CC (already pulled)
                    cycles += 15;
                } else {
                    PC = pullS16();
                    cycles += 6;
                }
                break;
            }
            // CWAI
            case 0x3C: { uint8_t pb = fetch8(); CC &= pb; CC |= CC_E;
                         pushRegs(0xFF, true); waiting = true; cycles += 20; break; }
            // MUL
            case 0x3D: { uint16_t r = (uint16_t)A * (uint16_t)B; setD(r);
                         setCC(CC_Z, r == 0); setCC(CC_C, B & 0x80); cycles += 11; break; }
            // SWI
            case 0x3F: { CC |= CC_E; pushRegs(0xFF, true); CC |= CC_I | CC_F;
                         PC = read16(0xFFFA); cycles += 19; break; }

            // NEGA
            case 0x40: { uint8_t r = op_sub8(0, A); A = r; cycles += 2; break; }
            // COMA
            case 0x43: A = ~A; setNZ8(A); setCC(CC_C, true); setCC(CC_V, false); cycles += 2; break;
            // LSRA
            case 0x44: setCC(CC_C, A & 1); A >>= 1; setNZ8(A); cycles += 2; break;
            // RORA
            case 0x46: { bool c = getCC(CC_C); setCC(CC_C, A & 1); A = (A >> 1) | (c ? 0x80 : 0); setNZ8(A); cycles += 2; break; }
            // ASRA
            case 0x47: setCC(CC_C, A & 1); A = (A >> 1) | (A & 0x80); setNZ8(A); cycles += 2; break;
            // ASLA
            case 0x48: { setCC(CC_C, A & 0x80); uint8_t r = A << 1; setCC(CC_V, ((A ^ r) & 0x80) != 0); A = r; setNZ8(A); cycles += 2; break; }
            // ROLA
            case 0x49: { bool c = getCC(CC_C); setCC(CC_C, A & 0x80); uint8_t r = (A << 1) | (c ? 1 : 0); setCC(CC_V, ((A ^ r) & 0x80) != 0); A = r; setNZ8(A); cycles += 2; break; }
            // DECA
            case 0x4A: setCC(CC_V, A == 0x80); A--; setNZ8(A); cycles += 2; break;
            // INCA
            case 0x4C: setCC(CC_V, A == 0x7F); A++; setNZ8(A); cycles += 2; break;
            // TSTA
            case 0x4D: setNZ8(A); setCC(CC_V, false); cycles += 2; break;
            // CLRA
            case 0x4F: A = 0; CC = (CC & ~(CC_N|CC_V|CC_C)) | CC_Z; cycles += 2; break;

            // NEGB
            case 0x50: { uint8_t r = op_sub8(0, B); B = r; cycles += 2; break; }
            // COMB
            case 0x53: B = ~B; setNZ8(B); setCC(CC_C, true); setCC(CC_V, false); cycles += 2; break;
            // LSRB
            case 0x54: setCC(CC_C, B & 1); B >>= 1; setNZ8(B); cycles += 2; break;
            // RORB
            case 0x56: { bool c = getCC(CC_C); setCC(CC_C, B & 1); B = (B >> 1) | (c ? 0x80 : 0); setNZ8(B); cycles += 2; break; }
            // ASRB
            case 0x57: setCC(CC_C, B & 1); B = (B >> 1) | (B & 0x80); setNZ8(B); cycles += 2; break;
            // ASLB
            case 0x58: { setCC(CC_C, B & 0x80); uint8_t r = B << 1; setCC(CC_V, ((B ^ r) & 0x80) != 0); B = r; setNZ8(B); cycles += 2; break; }
            // ROLB
            case 0x59: { bool c = getCC(CC_C); setCC(CC_C, B & 0x80); uint8_t r = (B << 1) | (c ? 1 : 0); setCC(CC_V, ((B ^ r) & 0x80) != 0); B = r; setNZ8(B); cycles += 2; break; }
            // DECB
            case 0x5A: setCC(CC_V, B == 0x80); B--; setNZ8(B); cycles += 2; break;
            // INCB
            case 0x5C: setCC(CC_V, B == 0x7F); B++; setNZ8(B); cycles += 2; break;
            // TSTB
            case 0x5D: setNZ8(B); setCC(CC_V, false); cycles += 2; break;
            // CLRB
            case 0x5F: B = 0; CC = (CC & ~(CC_N|CC_V|CC_C)) | CC_Z; cycles += 2; break;

            // NEG indexed
            case 0x60: { uint16_t ea = addrIndexed(); uint8_t v = read8(ea); uint8_t r = op_sub8(0, v);
                         write8(ea, r); cycles += 6; break; }
            // COM indexed
            case 0x63: { uint16_t ea = addrIndexed(); uint8_t v = ~read8(ea);
                         write8(ea, v); setNZ8(v); setCC(CC_C, true); setCC(CC_V, false); cycles += 6; break; }
            // LSR indexed
            case 0x64: { uint16_t ea = addrIndexed(); uint8_t v = read8(ea);
                         setCC(CC_C, v & 1); v >>= 1; setNZ8(v); write8(ea, v); cycles += 6; break; }
            // ROR indexed
            case 0x66: { uint16_t ea = addrIndexed(); uint8_t v = read8(ea); bool c = getCC(CC_C);
                         setCC(CC_C, v & 1); v = (v >> 1) | (c ? 0x80 : 0); setNZ8(v); write8(ea, v); cycles += 6; break; }
            // ASR indexed
            case 0x67: { uint16_t ea = addrIndexed(); uint8_t v = read8(ea);
                         setCC(CC_C, v & 1); v = (v >> 1) | (v & 0x80); setNZ8(v); write8(ea, v); cycles += 6; break; }
            // ASL indexed
            case 0x68: { uint16_t ea = addrIndexed(); uint8_t v = read8(ea);
                         setCC(CC_C, v & 0x80); uint8_t r = v << 1;
                         setNZ8(r); setCC(CC_V, ((v ^ r) & 0x80) != 0); write8(ea, r); cycles += 6; break; }
            // ROL indexed
            case 0x69: { uint16_t ea = addrIndexed(); uint8_t v = read8(ea); bool c = getCC(CC_C);
                         setCC(CC_C, v & 0x80); uint8_t r = (v << 1) | (c ? 1 : 0);
                         setNZ8(r); setCC(CC_V, ((v ^ r) & 0x80) != 0); write8(ea, r); cycles += 6; break; }
            // DEC indexed
            case 0x6A: { uint16_t ea = addrIndexed(); uint8_t v = read8(ea);
                         setCC(CC_V, v == 0x80); v--; setNZ8(v); write8(ea, v); cycles += 6; break; }
            // INC indexed
            case 0x6C: { uint16_t ea = addrIndexed(); uint8_t v = read8(ea);
                         setCC(CC_V, v == 0x7F); v++; setNZ8(v); write8(ea, v); cycles += 6; break; }
            // TST indexed
            case 0x6D: { uint16_t ea = addrIndexed(); uint8_t v = read8(ea);
                         setNZ8(v); setCC(CC_V, false); cycles += 6; break; }
            // JMP indexed
            case 0x6E: { PC = addrIndexed(); cycles += 3; break; }
            // CLR indexed
            case 0x6F: { uint16_t ea = addrIndexed(); write8(ea, 0);
                         CC = (CC & ~(CC_N|CC_V|CC_C)) | CC_Z; cycles += 6; break; }

            // NEG extended
            case 0x70: { uint16_t ea = addrExtended(); uint8_t v = read8(ea); uint8_t r = op_sub8(0, v);
                         write8(ea, r); cycles += 7; break; }
            // COM extended
            case 0x73: { uint16_t ea = addrExtended(); uint8_t v = ~read8(ea);
                         write8(ea, v); setNZ8(v); setCC(CC_C, true); setCC(CC_V, false); cycles += 7; break; }
            // LSR extended
            case 0x74: { uint16_t ea = addrExtended(); uint8_t v = read8(ea);
                         setCC(CC_C, v & 1); v >>= 1; setNZ8(v); write8(ea, v); cycles += 7; break; }
            // ROR extended
            case 0x76: { uint16_t ea = addrExtended(); uint8_t v = read8(ea); bool c = getCC(CC_C);
                         setCC(CC_C, v & 1); v = (v >> 1) | (c ? 0x80 : 0); setNZ8(v); write8(ea, v); cycles += 7; break; }
            // ASR extended
            case 0x77: { uint16_t ea = addrExtended(); uint8_t v = read8(ea);
                         setCC(CC_C, v & 1); v = (v >> 1) | (v & 0x80); setNZ8(v); write8(ea, v); cycles += 7; break; }
            // ASL extended
            case 0x78: { uint16_t ea = addrExtended(); uint8_t v = read8(ea);
                         setCC(CC_C, v & 0x80); uint8_t r = v << 1;
                         setNZ8(r); setCC(CC_V, ((v ^ r) & 0x80) != 0); write8(ea, r); cycles += 7; break; }
            // ROL extended
            case 0x79: { uint16_t ea = addrExtended(); uint8_t v = read8(ea); bool c = getCC(CC_C);
                         setCC(CC_C, v & 0x80); uint8_t r = (v << 1) | (c ? 1 : 0);
                         setNZ8(r); setCC(CC_V, ((v ^ r) & 0x80) != 0); write8(ea, r); cycles += 7; break; }
            // DEC extended
            case 0x7A: { uint16_t ea = addrExtended(); uint8_t v = read8(ea);
                         setCC(CC_V, v == 0x80); v--; setNZ8(v); write8(ea, v); cycles += 7; break; }
            // INC extended
            case 0x7C: { uint16_t ea = addrExtended(); uint8_t v = read8(ea);
                         setCC(CC_V, v == 0x7F); v++; setNZ8(v); write8(ea, v); cycles += 7; break; }
            // TST extended
            case 0x7D: { uint16_t ea = addrExtended(); uint8_t v = read8(ea);
                         setNZ8(v); setCC(CC_V, false); cycles += 7; break; }
            // JMP extended
            case 0x7E: { PC = addrExtended(); cycles += 4; break; }
            // CLR extended
            case 0x7F: { uint16_t ea = addrExtended(); write8(ea, 0);
                         CC = (CC & ~(CC_N|CC_V|CC_C)) | CC_Z; cycles += 7; break; }

            // SUBA immediate
            case 0x80: A = op_sub8(A, fetch8()); cycles += 2; break;
            // CMPA immediate
            case 0x81: op_sub8(A, fetch8()); cycles += 2; break;
            // SBCA immediate
            case 0x82: A = op_sub8(A, fetch8(), true); cycles += 2; break;
            // SUBD immediate
            case 0x83: setD(op_sub16(getD(), fetch16())); cycles += 4; break;
            // ANDA immediate
            case 0x84: A &= fetch8(); setNZ8(A); setCC(CC_V, false); cycles += 2; break;
            // BITA immediate
            case 0x85: { uint8_t r = A & fetch8(); setNZ8(r); setCC(CC_V, false); cycles += 2; break; }
            // LDA immediate
            case 0x86: A = fetch8(); setNZ8(A); setCC(CC_V, false); cycles += 2; break;
            // EORA immediate
            case 0x88: A ^= fetch8(); setNZ8(A); setCC(CC_V, false); cycles += 2; break;
            // ADCA immediate
            case 0x89: A = op_add8(A, fetch8(), true); cycles += 2; break;
            // ORA immediate
            case 0x8A: A |= fetch8(); setNZ8(A); setCC(CC_V, false); cycles += 2; break;
            // ADDA immediate
            case 0x8B: A = op_add8(A, fetch8()); cycles += 2; break;
            // CMPX immediate
            case 0x8C: op_sub16(X, fetch16()); cycles += 4; break;
            // BSR
            case 0x8D: { int8_t off = (int8_t)fetch8(); pushS16(PC); PC += off; cycles += 7; break; }
            // LDX immediate
            case 0x8E: X = fetch16(); setNZ16(X); setCC(CC_V, false); cycles += 3; break;

            // SUBA direct
            case 0x90: { uint16_t ea = addrDirect(); A = op_sub8(A, read8(ea)); cycles += 4; break; }
            // CMPA direct
            case 0x91: { uint16_t ea = addrDirect(); op_sub8(A, read8(ea)); cycles += 4; break; }
            // SBCA direct
            case 0x92: { uint16_t ea = addrDirect(); A = op_sub8(A, read8(ea), true); cycles += 4; break; }
            // SUBD direct
            case 0x93: { uint16_t ea = addrDirect(); setD(op_sub16(getD(), read16(ea))); cycles += 6; break; }
            // ANDA direct
            case 0x94: { uint16_t ea = addrDirect(); A &= read8(ea); setNZ8(A); setCC(CC_V, false); cycles += 4; break; }
            // BITA direct
            case 0x95: { uint16_t ea = addrDirect(); uint8_t r = A & read8(ea); setNZ8(r); setCC(CC_V, false); cycles += 4; break; }
            // LDA direct
            case 0x96: { uint16_t ea = addrDirect(); A = read8(ea); setNZ8(A); setCC(CC_V, false); cycles += 4; break; }
            // STA direct
            case 0x97: { uint16_t ea = addrDirect(); write8(ea, A); setNZ8(A); setCC(CC_V, false); cycles += 4; break; }
            // EORA direct
            case 0x98: { uint16_t ea = addrDirect(); A ^= read8(ea); setNZ8(A); setCC(CC_V, false); cycles += 4; break; }
            // ADCA direct
            case 0x99: { uint16_t ea = addrDirect(); A = op_add8(A, read8(ea), true); cycles += 4; break; }
            // ORA direct
            case 0x9A: { uint16_t ea = addrDirect(); A |= read8(ea); setNZ8(A); setCC(CC_V, false); cycles += 4; break; }
            // ADDA direct
            case 0x9B: { uint16_t ea = addrDirect(); A = op_add8(A, read8(ea)); cycles += 4; break; }
            // CMPX direct
            case 0x9C: { uint16_t ea = addrDirect(); op_sub16(X, read16(ea)); cycles += 6; break; }
            // JSR direct
            case 0x9D: { uint16_t ea = addrDirect(); pushS16(PC); PC = ea; cycles += 7; break; }
            // LDX direct
            case 0x9E: { uint16_t ea = addrDirect(); X = read16(ea); setNZ16(X); setCC(CC_V, false); cycles += 5; break; }
            // STX direct
            case 0x9F: { uint16_t ea = addrDirect(); write16(ea, X); setNZ16(X); setCC(CC_V, false); cycles += 5; break; }

            // SUBA indexed
            case 0xA0: { uint16_t ea = addrIndexed(); A = op_sub8(A, read8(ea)); cycles += 4; break; }
            // CMPA indexed
            case 0xA1: { uint16_t ea = addrIndexed(); op_sub8(A, read8(ea)); cycles += 4; break; }
            // SBCA indexed
            case 0xA2: { uint16_t ea = addrIndexed(); A = op_sub8(A, read8(ea), true); cycles += 4; break; }
            // SUBD indexed
            case 0xA3: { uint16_t ea = addrIndexed(); setD(op_sub16(getD(), read16(ea))); cycles += 6; break; }
            // ANDA indexed
            case 0xA4: { uint16_t ea = addrIndexed(); A &= read8(ea); setNZ8(A); setCC(CC_V, false); cycles += 4; break; }
            // BITA indexed
            case 0xA5: { uint16_t ea = addrIndexed(); uint8_t r = A & read8(ea); setNZ8(r); setCC(CC_V, false); cycles += 4; break; }
            // LDA indexed
            case 0xA6: { uint16_t ea = addrIndexed(); A = read8(ea); setNZ8(A); setCC(CC_V, false); cycles += 4; break; }
            // STA indexed
            case 0xA7: { uint16_t ea = addrIndexed(); write8(ea, A); setNZ8(A); setCC(CC_V, false); cycles += 4; break; }
            // EORA indexed
            case 0xA8: { uint16_t ea = addrIndexed(); A ^= read8(ea); setNZ8(A); setCC(CC_V, false); cycles += 4; break; }
            // ADCA indexed
            case 0xA9: { uint16_t ea = addrIndexed(); A = op_add8(A, read8(ea), true); cycles += 4; break; }
            // ORA indexed
            case 0xAA: { uint16_t ea = addrIndexed(); A |= read8(ea); setNZ8(A); setCC(CC_V, false); cycles += 4; break; }
            // ADDA indexed
            case 0xAB: { uint16_t ea = addrIndexed(); A = op_add8(A, read8(ea)); cycles += 4; break; }
            // CMPX indexed
            case 0xAC: { uint16_t ea = addrIndexed(); op_sub16(X, read16(ea)); cycles += 6; break; }
            // JSR indexed
            case 0xAD: { uint16_t ea = addrIndexed(); pushS16(PC); PC = ea; cycles += 7; break; }
            // LDX indexed
            case 0xAE: { uint16_t ea = addrIndexed(); X = read16(ea); setNZ16(X); setCC(CC_V, false); cycles += 5; break; }
            // STX indexed
            case 0xAF: { uint16_t ea = addrIndexed(); write16(ea, X); setNZ16(X); setCC(CC_V, false); cycles += 5; break; }

            // SUBA extended
            case 0xB0: { uint16_t ea = addrExtended(); A = op_sub8(A, read8(ea)); cycles += 5; break; }
            // CMPA extended
            case 0xB1: { uint16_t ea = addrExtended(); op_sub8(A, read8(ea)); cycles += 5; break; }
            // SBCA extended
            case 0xB2: { uint16_t ea = addrExtended(); A = op_sub8(A, read8(ea), true); cycles += 5; break; }
            // SUBD extended
            case 0xB3: { uint16_t ea = addrExtended(); setD(op_sub16(getD(), read16(ea))); cycles += 7; break; }
            // ANDA extended
            case 0xB4: { uint16_t ea = addrExtended(); A &= read8(ea); setNZ8(A); setCC(CC_V, false); cycles += 5; break; }
            // BITA extended
            case 0xB5: { uint16_t ea = addrExtended(); uint8_t r = A & read8(ea); setNZ8(r); setCC(CC_V, false); cycles += 5; break; }
            // LDA extended
            case 0xB6: { uint16_t ea = addrExtended(); A = read8(ea); setNZ8(A); setCC(CC_V, false); cycles += 5; break; }
            // STA extended
            case 0xB7: { uint16_t ea = addrExtended(); write8(ea, A); setNZ8(A); setCC(CC_V, false); cycles += 5; break; }
            // EORA extended
            case 0xB8: { uint16_t ea = addrExtended(); A ^= read8(ea); setNZ8(A); setCC(CC_V, false); cycles += 5; break; }
            // ADCA extended
            case 0xB9: { uint16_t ea = addrExtended(); A = op_add8(A, read8(ea), true); cycles += 5; break; }
            // ORA extended
            case 0xBA: { uint16_t ea = addrExtended(); A |= read8(ea); setNZ8(A); setCC(CC_V, false); cycles += 5; break; }
            // ADDA extended
            case 0xBB: { uint16_t ea = addrExtended(); A = op_add8(A, read8(ea)); cycles += 5; break; }
            // CMPX extended
            case 0xBC: { uint16_t ea = addrExtended(); op_sub16(X, read16(ea)); cycles += 7; break; }
            // JSR extended
            case 0xBD: { uint16_t ea = addrExtended(); pushS16(PC); PC = ea; cycles += 8; break; }
            // LDX extended
            case 0xBE: { uint16_t ea = addrExtended(); X = read16(ea); setNZ16(X); setCC(CC_V, false); cycles += 6; break; }
            // STX extended
            case 0xBF: { uint16_t ea = addrExtended(); write16(ea, X); setNZ16(X); setCC(CC_V, false); cycles += 6; break; }

            // SUBB immediate
            case 0xC0: B = op_sub8(B, fetch8()); cycles += 2; break;
            // CMPB immediate
            case 0xC1: op_sub8(B, fetch8()); cycles += 2; break;
            // SBCB immediate
            case 0xC2: B = op_sub8(B, fetch8(), true); cycles += 2; break;
            // ADDD immediate
            case 0xC3: setD(op_add16(getD(), fetch16())); cycles += 4; break;
            // ANDB immediate
            case 0xC4: B &= fetch8(); setNZ8(B); setCC(CC_V, false); cycles += 2; break;
            // BITB immediate
            case 0xC5: { uint8_t r = B & fetch8(); setNZ8(r); setCC(CC_V, false); cycles += 2; break; }
            // LDB immediate
            case 0xC6: B = fetch8(); setNZ8(B); setCC(CC_V, false); cycles += 2; break;
            // EORB immediate
            case 0xC8: B ^= fetch8(); setNZ8(B); setCC(CC_V, false); cycles += 2; break;
            // ADCB immediate
            case 0xC9: B = op_add8(B, fetch8(), true); cycles += 2; break;
            // ORB immediate
            case 0xCA: B |= fetch8(); setNZ8(B); setCC(CC_V, false); cycles += 2; break;
            // ADDB immediate
            case 0xCB: B = op_add8(B, fetch8()); cycles += 2; break;
            // LDD immediate
            case 0xCC: setD(fetch16()); setNZ16(getD()); setCC(CC_V, false); cycles += 3; break;
            // LDU immediate
            case 0xCE: U = fetch16(); setNZ16(U); setCC(CC_V, false); cycles += 3; break;

            // SUBB direct
            case 0xD0: { uint16_t ea = addrDirect(); B = op_sub8(B, read8(ea)); cycles += 4; break; }
            // CMPB direct
            case 0xD1: { uint16_t ea = addrDirect(); op_sub8(B, read8(ea)); cycles += 4; break; }
            // SBCB direct
            case 0xD2: { uint16_t ea = addrDirect(); B = op_sub8(B, read8(ea), true); cycles += 4; break; }
            // ADDD direct
            case 0xD3: { uint16_t ea = addrDirect(); setD(op_add16(getD(), read16(ea))); cycles += 6; break; }
            // ANDB direct
            case 0xD4: { uint16_t ea = addrDirect(); B &= read8(ea); setNZ8(B); setCC(CC_V, false); cycles += 4; break; }
            // BITB direct
            case 0xD5: { uint16_t ea = addrDirect(); uint8_t r = B & read8(ea); setNZ8(r); setCC(CC_V, false); cycles += 4; break; }
            // LDB direct
            case 0xD6: { uint16_t ea = addrDirect(); B = read8(ea); setNZ8(B); setCC(CC_V, false); cycles += 4; break; }
            // STB direct
            case 0xD7: { uint16_t ea = addrDirect(); write8(ea, B); setNZ8(B); setCC(CC_V, false); cycles += 4; break; }
            // EORB direct
            case 0xD8: { uint16_t ea = addrDirect(); B ^= read8(ea); setNZ8(B); setCC(CC_V, false); cycles += 4; break; }
            // ADCB direct
            case 0xD9: { uint16_t ea = addrDirect(); B = op_add8(B, read8(ea), true); cycles += 4; break; }
            // ORB direct
            case 0xDA: { uint16_t ea = addrDirect(); B |= read8(ea); setNZ8(B); setCC(CC_V, false); cycles += 4; break; }
            // ADDB direct
            case 0xDB: { uint16_t ea = addrDirect(); B = op_add8(B, read8(ea)); cycles += 4; break; }
            // LDD direct
            case 0xDC: { uint16_t ea = addrDirect(); setD(read16(ea)); setNZ16(getD()); setCC(CC_V, false); cycles += 5; break; }
            // STD direct
            case 0xDD: { uint16_t ea = addrDirect(); write16(ea, getD()); setNZ16(getD()); setCC(CC_V, false); cycles += 5; break; }
            // LDU direct
            case 0xDE: { uint16_t ea = addrDirect(); U = read16(ea); setNZ16(U); setCC(CC_V, false); cycles += 5; break; }
            // STU direct
            case 0xDF: { uint16_t ea = addrDirect(); write16(ea, U); setNZ16(U); setCC(CC_V, false); cycles += 5; break; }

            // SUBB indexed
            case 0xE0: { uint16_t ea = addrIndexed(); B = op_sub8(B, read8(ea)); cycles += 4; break; }
            // CMPB indexed
            case 0xE1: { uint16_t ea = addrIndexed(); op_sub8(B, read8(ea)); cycles += 4; break; }
            // SBCB indexed
            case 0xE2: { uint16_t ea = addrIndexed(); B = op_sub8(B, read8(ea), true); cycles += 4; break; }
            // ADDD indexed
            case 0xE3: { uint16_t ea = addrIndexed(); setD(op_add16(getD(), read16(ea))); cycles += 6; break; }
            // ANDB indexed
            case 0xE4: { uint16_t ea = addrIndexed(); B &= read8(ea); setNZ8(B); setCC(CC_V, false); cycles += 4; break; }
            // BITB indexed
            case 0xE5: { uint16_t ea = addrIndexed(); uint8_t r = B & read8(ea); setNZ8(r); setCC(CC_V, false); cycles += 4; break; }
            // LDB indexed
            case 0xE6: { uint16_t ea = addrIndexed(); B = read8(ea); setNZ8(B); setCC(CC_V, false); cycles += 4; break; }
            // STB indexed
            case 0xE7: { uint16_t ea = addrIndexed(); write8(ea, B); setNZ8(B); setCC(CC_V, false); cycles += 4; break; }
            // EORB indexed
            case 0xE8: { uint16_t ea = addrIndexed(); B ^= read8(ea); setNZ8(B); setCC(CC_V, false); cycles += 4; break; }
            // ADCB indexed
            case 0xE9: { uint16_t ea = addrIndexed(); B = op_add8(B, read8(ea), true); cycles += 4; break; }
            // ORB indexed
            case 0xEA: { uint16_t ea = addrIndexed(); B |= read8(ea); setNZ8(B); setCC(CC_V, false); cycles += 4; break; }
            // ADDB indexed
            case 0xEB: { uint16_t ea = addrIndexed(); B = op_add8(B, read8(ea)); cycles += 4; break; }
            // LDD indexed
            case 0xEC: { uint16_t ea = addrIndexed(); setD(read16(ea)); setNZ16(getD()); setCC(CC_V, false); cycles += 5; break; }
            // STD indexed
            case 0xED: { uint16_t ea = addrIndexed(); write16(ea, getD()); setNZ16(getD()); setCC(CC_V, false); cycles += 5; break; }
            // LDU indexed
            case 0xEE: { uint16_t ea = addrIndexed(); U = read16(ea); setNZ16(U); setCC(CC_V, false); cycles += 5; break; }
            // STU indexed
            case 0xEF: { uint16_t ea = addrIndexed(); write16(ea, U); setNZ16(U); setCC(CC_V, false); cycles += 5; break; }

            // SUBB extended
            case 0xF0: { uint16_t ea = addrExtended(); B = op_sub8(B, read8(ea)); cycles += 5; break; }
            // CMPB extended
            case 0xF1: { uint16_t ea = addrExtended(); op_sub8(B, read8(ea)); cycles += 5; break; }
            // SBCB extended
            case 0xF2: { uint16_t ea = addrExtended(); B = op_sub8(B, read8(ea), true); cycles += 5; break; }
            // ADDD extended
            case 0xF3: { uint16_t ea = addrExtended(); setD(op_add16(getD(), read16(ea))); cycles += 7; break; }
            // ANDB extended
            case 0xF4: { uint16_t ea = addrExtended(); B &= read8(ea); setNZ8(B); setCC(CC_V, false); cycles += 5; break; }
            // BITB extended
            case 0xF5: { uint16_t ea = addrExtended(); uint8_t r = B & read8(ea); setNZ8(r); setCC(CC_V, false); cycles += 5; break; }
            // LDB extended
            case 0xF6: { uint16_t ea = addrExtended(); B = read8(ea); setNZ8(B); setCC(CC_V, false); cycles += 5; break; }
            // STB extended
            case 0xF7: { uint16_t ea = addrExtended(); write8(ea, B); setNZ8(B); setCC(CC_V, false); cycles += 5; break; }
            // EORB extended
            case 0xF8: { uint16_t ea = addrExtended(); B ^= read8(ea); setNZ8(B); setCC(CC_V, false); cycles += 5; break; }
            // ADCB extended
            case 0xF9: { uint16_t ea = addrExtended(); B = op_add8(B, read8(ea), true); cycles += 5; break; }
            // ORB extended
            case 0xFA: { uint16_t ea = addrExtended(); B |= read8(ea); setNZ8(B); setCC(CC_V, false); cycles += 5; break; }
            // ADDB extended
            case 0xFB: { uint16_t ea = addrExtended(); B = op_add8(B, read8(ea)); cycles += 5; break; }
            // LDD extended
            case 0xFC: { uint16_t ea = addrExtended(); setD(read16(ea)); setNZ16(getD()); setCC(CC_V, false); cycles += 6; break; }
            // STD extended
            case 0xFD: { uint16_t ea = addrExtended(); write16(ea, getD()); setNZ16(getD()); setCC(CC_V, false); cycles += 6; break; }
            // LDU extended
            case 0xFE: { uint16_t ea = addrExtended(); U = read16(ea); setNZ16(U); setCC(CC_V, false); cycles += 6; break; }
            // STU extended
            case 0xFF: { uint16_t ea = addrExtended(); write16(ea, U); setNZ16(U); setCC(CC_V, false); cycles += 6; break; }

            default:
                // Illegal opcode - treat as NOP
                cycles += 2;
                break;
        }

        totalCycles += cycles;
        return cycles;
    }

    // Page 2 instructions (0x10 prefix)
    int executePage2() {
        uint8_t op = fetch8();
        switch (op) {
            // LBRN
            case 0x21: fetch16(); cycles += 5; break;
            // LBHI
            case 0x22: { int16_t off = (int16_t)fetch16(); if (!(CC & (CC_C|CC_Z))) { PC += off; cycles += 6; } else cycles += 5; break; }
            // LBLS
            case 0x23: { int16_t off = (int16_t)fetch16(); if (CC & (CC_C|CC_Z)) { PC += off; cycles += 6; } else cycles += 5; break; }
            // LBCC
            case 0x24: { int16_t off = (int16_t)fetch16(); if (!(CC & CC_C)) { PC += off; cycles += 6; } else cycles += 5; break; }
            // LBCS
            case 0x25: { int16_t off = (int16_t)fetch16(); if (CC & CC_C) { PC += off; cycles += 6; } else cycles += 5; break; }
            // LBNE
            case 0x26: { int16_t off = (int16_t)fetch16(); if (!(CC & CC_Z)) { PC += off; cycles += 6; } else cycles += 5; break; }
            // LBEQ
            case 0x27: { int16_t off = (int16_t)fetch16(); if (CC & CC_Z) { PC += off; cycles += 6; } else cycles += 5; break; }
            // LBVC
            case 0x28: { int16_t off = (int16_t)fetch16(); if (!(CC & CC_V)) { PC += off; cycles += 6; } else cycles += 5; break; }
            // LBVS
            case 0x29: { int16_t off = (int16_t)fetch16(); if (CC & CC_V) { PC += off; cycles += 6; } else cycles += 5; break; }
            // LBPL
            case 0x2A: { int16_t off = (int16_t)fetch16(); if (!(CC & CC_N)) { PC += off; cycles += 6; } else cycles += 5; break; }
            // LBMI
            case 0x2B: { int16_t off = (int16_t)fetch16(); if (CC & CC_N) { PC += off; cycles += 6; } else cycles += 5; break; }
            // LBGE
            case 0x2C: { int16_t off = (int16_t)fetch16(); bool n = getCC(CC_N), v = getCC(CC_V);
                         if (n == v) { PC += off; cycles += 6; } else cycles += 5; break; }
            // LBLT
            case 0x2D: { int16_t off = (int16_t)fetch16(); bool n = getCC(CC_N), v = getCC(CC_V);
                         if (n != v) { PC += off; cycles += 6; } else cycles += 5; break; }
            // LBGT
            case 0x2E: { int16_t off = (int16_t)fetch16(); bool n = getCC(CC_N), v = getCC(CC_V);
                         if (!getCC(CC_Z) && n == v) { PC += off; cycles += 6; } else cycles += 5; break; }
            // LBLE
            case 0x2F: { int16_t off = (int16_t)fetch16(); bool n = getCC(CC_N), v = getCC(CC_V);
                         if (getCC(CC_Z) || n != v) { PC += off; cycles += 6; } else cycles += 5; break; }

            // SWI2
            case 0x3F: { CC |= CC_E; pushRegs(0xFF, true);
                         PC = read16(0xFFF4); cycles += 20; break; }

            // CMPD immediate
            case 0x83: op_sub16(getD(), fetch16()); cycles += 5; break;
            // CMPY immediate
            case 0x8C: op_sub16(Y, fetch16()); cycles += 5; break;
            // LDY immediate
            case 0x8E: Y = fetch16(); setNZ16(Y); setCC(CC_V, false); cycles += 4; break;

            // CMPD direct
            case 0x93: { uint16_t ea = addrDirect(); op_sub16(getD(), read16(ea)); cycles += 7; break; }
            // CMPY direct
            case 0x9C: { uint16_t ea = addrDirect(); op_sub16(Y, read16(ea)); cycles += 7; break; }
            // LDY direct
            case 0x9E: { uint16_t ea = addrDirect(); Y = read16(ea); setNZ16(Y); setCC(CC_V, false); cycles += 6; break; }
            // STY direct
            case 0x9F: { uint16_t ea = addrDirect(); write16(ea, Y); setNZ16(Y); setCC(CC_V, false); cycles += 6; break; }

            // CMPD indexed
            case 0xA3: { uint16_t ea = addrIndexed(); op_sub16(getD(), read16(ea)); cycles += 7; break; }
            // CMPY indexed
            case 0xAC: { uint16_t ea = addrIndexed(); op_sub16(Y, read16(ea)); cycles += 7; break; }
            // LDY indexed
            case 0xAE: { uint16_t ea = addrIndexed(); Y = read16(ea); setNZ16(Y); setCC(CC_V, false); cycles += 6; break; }
            // STY indexed
            case 0xAF: { uint16_t ea = addrIndexed(); write16(ea, Y); setNZ16(Y); setCC(CC_V, false); cycles += 6; break; }

            // CMPD extended
            case 0xB3: { uint16_t ea = addrExtended(); op_sub16(getD(), read16(ea)); cycles += 8; break; }
            // CMPY extended
            case 0xBC: { uint16_t ea = addrExtended(); op_sub16(Y, read16(ea)); cycles += 8; break; }
            // LDY extended
            case 0xBE: { uint16_t ea = addrExtended(); Y = read16(ea); setNZ16(Y); setCC(CC_V, false); cycles += 7; break; }
            // STY extended
            case 0xBF: { uint16_t ea = addrExtended(); write16(ea, Y); setNZ16(Y); setCC(CC_V, false); cycles += 7; break; }

            // LDS immediate
            case 0xCE: S = fetch16(); setNZ16(S); setCC(CC_V, false); nmiArmed = true; cycles += 4; break;
            // LDS direct
            case 0xDE: { uint16_t ea = addrDirect(); S = read16(ea); setNZ16(S); setCC(CC_V, false); nmiArmed = true; cycles += 6; break; }
            // STS direct
            case 0xDF: { uint16_t ea = addrDirect(); write16(ea, S); setNZ16(S); setCC(CC_V, false); cycles += 6; break; }
            // LDS indexed
            case 0xEE: { uint16_t ea = addrIndexed(); S = read16(ea); setNZ16(S); setCC(CC_V, false); nmiArmed = true; cycles += 6; break; }
            // STS indexed
            case 0xEF: { uint16_t ea = addrIndexed(); write16(ea, S); setNZ16(S); setCC(CC_V, false); cycles += 6; break; }
            // LDS extended
            case 0xFE: { uint16_t ea = addrExtended(); S = read16(ea); setNZ16(S); setCC(CC_V, false); nmiArmed = true; cycles += 7; break; }
            // STS extended
            case 0xFF: { uint16_t ea = addrExtended(); write16(ea, S); setNZ16(S); setCC(CC_V, false); cycles += 7; break; }

            default: cycles += 2; break;
        }
        totalCycles += cycles;
        return cycles;
    }

    // Page 3 instructions (0x11 prefix)
    int executePage3() {
        uint8_t op = fetch8();
        switch (op) {
            // SWI3
            case 0x3F: { CC |= CC_E; pushRegs(0xFF, true);
                         PC = read16(0xFFF2); cycles += 20; break; }

            // CMPU immediate
            case 0x83: op_sub16(U, fetch16()); cycles += 5; break;
            // CMPS immediate
            case 0x8C: op_sub16(S, fetch16()); cycles += 5; break;

            // CMPU direct
            case 0x93: { uint16_t ea = addrDirect(); op_sub16(U, read16(ea)); cycles += 7; break; }
            // CMPS direct
            case 0x9C: { uint16_t ea = addrDirect(); op_sub16(S, read16(ea)); cycles += 7; break; }

            // CMPU indexed
            case 0xA3: { uint16_t ea = addrIndexed(); op_sub16(U, read16(ea)); cycles += 7; break; }
            // CMPS indexed
            case 0xAC: { uint16_t ea = addrIndexed(); op_sub16(S, read16(ea)); cycles += 7; break; }

            // CMPU extended
            case 0xB3: { uint16_t ea = addrExtended(); op_sub16(U, read16(ea)); cycles += 8; break; }
            // CMPS extended
            case 0xBC: { uint16_t ea = addrExtended(); op_sub16(S, read16(ea)); cycles += 8; break; }

            default: cycles += 2; break;
        }
        totalCycles += cycles;
        return cycles;
    }
};
