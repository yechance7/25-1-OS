// vmsim.cpp
#include <iostream>
#include <iomanip>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <list>
#include <sstream>
#include <bitset>
#include <cstdint>
#include <cstring>
#include <cassert>

// Page Table Entry 구조체
struct PageTableEntry {
    int pfn;
    bool valid;
};

// 2레벨 페이지 테이블
PageTableEntry page_directory[1024][1024];
int next_free_pfn = 0;

void translate(uint32_t va) {
    int pdi = (va >> 22) & 0x3FF;
    int pti = (va >> 12) & 0x3FF;
    int offset = va & 0xFFF;

    PageTableEntry& entry = page_directory[pdi][pti];

    bool page_fault = false;
    if (!entry.valid) {
        entry.valid = true;
        entry.pfn = next_free_pfn++;
        page_fault = true;
    }

    uint32_t pa = (entry.pfn << 12) | offset;

    std::cout << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << va
          << " -> 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << pa
          << ", " << (page_fault ? "Page fault" : "No page fault") << std::endl;

}

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        uint32_t va;
        ss >> std::hex >> va;
        translate(va);
    }
    return 0;
}
