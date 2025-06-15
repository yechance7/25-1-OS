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

// TLB 구조체
struct TLBEntry {
    uint32_t vpn;
    int pfn;
    bool valid;
};

int TLB_SIZE = 4; // 기본값. main에서 인자 파싱하도록 수정 필요
std::list<TLBEntry> tlb;

// VPN 계산 함수
uint32_t get_vpn(uint32_t va) {
    return va >> 12;
}

// TLB에서 VPN 검색
bool tlb_lookup(uint32_t vpn, int& pfn) {
    for (auto it = tlb.begin(); it != tlb.end(); ++it) {
        if (it->valid && it->vpn == vpn) {
            pfn = it->pfn;
            return true;
        }
    }
    return false;
}

// TLB 업데이트 (FIFO 방식)
void tlb_update(uint32_t vpn, int pfn) {
    for (auto it = tlb.begin(); it != tlb.end(); ++it) {
        if (it->vpn == vpn) {
            tlb.erase(it);
            break;
        }
    }
    if ((int)tlb.size() >= TLB_SIZE) {
        tlb.pop_front();
    }
    tlb.push_back({vpn, pfn, true});
}

// TLB에서 해당 VPN 제거
void tlb_invalidate(uint32_t vpn) {
    for (auto it = tlb.begin(); it != tlb.end(); ++it) {
        if (it->vpn == vpn) {
            tlb.erase(it);
            break;
        }
    }
}


void translate(uint32_t va) {
    int pdi = (va >> 22) & 0x3FF;
    int pti = (va >> 12) & 0x3FF;
    int offset = va & 0xFFF;
    uint32_t vpn = get_vpn(va);
    int pfn;
    
    std::string tlb_result;
    std::string page_fault_result;
    
    if (tlb_lookup(vpn, pfn)) {
        tlb_result = "TLB hit";
        page_fault_result = "No page fault";
    } else {
        tlb_result = "TLB miss";
        PageTableEntry& entry = page_directory[pdi][pti];

        bool page_fault = false;
        if (!entry.valid) {
            entry.valid = true;
            entry.pfn = next_free_pfn++;
            page_fault = true;
        }

        pfn = entry.pfn;
        tlb_update(vpn, pfn);
        page_fault_result = page_fault ? "Page fault" : "No page fault";
    }

    uint32_t pa = (pfn << 12) | offset;

    std::cout << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << va
              << " -> 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << pa
              << ", " << tlb_result << ", " << page_fault_result << std::endl;
}


int main(int argc, char* argv[]) {
    if (argc >= 3) {
        TLB_SIZE = std::stoi(argv[2]);
    }

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

