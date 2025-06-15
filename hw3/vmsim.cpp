#include <iostream>
#include <iomanip>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <list>
#include <queue>
#include <sstream>
#include <bitset>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <optional>
#include <algorithm>
#include <cmath>
#include <memory>
#include <climits>
#include <fstream>   
#include <chrono>   
#include <ctime>     
#include <string>    
#include <sys/stat.h> 

using namespace std;

// S3FIFO 사용 시 디버그 로그 파일
ofstream debug_log_file;

// 페이지 테이블 엔트리 구조체: 물리 프레임 번호와 유효 비트.
struct PageTableEntry {
    int pfn;
    bool valid;
};

// 2단계 페이지 테이블의 최상위 디렉토리.
PageTableEntry page_directory[1024][1024];
// 다음 할당될 물리 프레임 번호.
int next_free_pfn = 0;

// 총 물리 프레임 수와 TLB 크기.
int TOTAL_FRAMES = 0;
int TLB_SIZE = 0;

// 시뮬레이션 통계 카운터.
int total_refs = 0;
int tlb_hits = 0, tlb_misses = 0;
int page_faults = 0;

// 페이지 교체 시 반환 정보.
struct EvictionResultInfo {
    optional<uint32_t> vpn; // 교체된 가상 페이지 번호
    optional<uint32_t> va;  // 교체된 가상 주소
};

// TLB 엔트리 구조체.
struct TLBEntry {
    uint32_t vpn;
    int pfn;
    bool valid;
};

// TLB는 Fully-associative 캐시로 구현.
list<TLBEntry> tlb;

// 가상 주소(va)로부터 가상 페이지 번호(vpn) 추출.
uint32_t get_vpn(uint32_t va) {
    return va >> 12;
}

// 페이지 교체 알고리즘 인터페이스.
class ReplacementPolicy {
public:
    virtual void access(uint32_t vpn) = 0; // 페이지 접근
    virtual void insert(uint32_t vpn) = 0; // 새 페이지 삽입
    virtual optional<uint32_t> evict_if_needed() = 0; // 캐시 용량 초과 시 희생자 반환
    virtual void erase(uint32_t vpn) = 0; // 특정 페이지 제거
    virtual ~ReplacementPolicy() = default; // 소멸자
};

// FIFO 페이지 교체 정책.
class FIFOReplacement : public ReplacementPolicy {
    list<uint32_t> queue; // 삽입 순서 큐
    unordered_set<uint32_t> in_cache; // 캐시 내 항목 존재 확인
    int capacity; // 캐시 용량
public:
    FIFOReplacement(int cap) : capacity(cap) {} // 생성자
    void access(uint32_t) override {} // 접근 순서 무관
    void insert(uint32_t vpn) override { // 페이지 삽입
        if (in_cache.count(vpn)) return;
        queue.push_back(vpn);
        in_cache.insert(vpn);
    }
    optional<uint32_t> evict_if_needed() override { // 교체 필요 시
        if ((int)queue.size() > capacity) {
            uint32_t victim = queue.front(); queue.pop_front();
            in_cache.erase(victim);
            return victim;
        }
        return nullopt;
    }
    void erase(uint32_t vpn) override { // 특정 페이지 제거
        if (in_cache.erase(vpn)) queue.remove(vpn);
    }
};

// LRU 페이지 교체 정책.
class LRUReplacement : public ReplacementPolicy {
    list<uint32_t> lru; // 최근 사용 순서 리스트
    unordered_map<uint32_t, list<uint32_t>::iterator> map; // VPN-리스트 반복자 매핑
    int capacity; // 캐시 용량
public:
    LRUReplacement(int cap) : capacity(cap) {} // 생성자
    void access(uint32_t vpn) override { // 페이지 접근
        if (map.count(vpn)) {
            lru.erase(map[vpn]);
            lru.push_back(vpn);
            map[vpn] = --lru.end();
        }
    }
    void insert(uint32_t vpn) override { // 페이지 삽입
        if (map.count(vpn)) return;
        lru.push_back(vpn);
        map[vpn] = --lru.end();
    }
    optional<uint32_t> evict_if_needed() override { // 교체 필요 시
        if ((int)lru.size() > capacity) {
            uint32_t victim = lru.front(); lru.pop_front();
            map.erase(victim);
            return victim;
        }
        return nullopt;
    }
    void erase(uint32_t vpn) override { // 특정 페이지 제거
        if (map.count(vpn)) {
            lru.erase(map[vpn]);
            map.erase(vpn);
        }
    }
};

// LFU 페이지 교체 정책.
class LFUReplacement : public ReplacementPolicy {
    unordered_map<uint32_t, int> freq; // VPN별 접근 빈도
    list<uint32_t> order; // 페이지 삽입 순서 (동률 시 사용)
    int capacity; // 캐시 용량
public:
    LFUReplacement(int cap) : capacity(cap) {} // 생성자
    void access(uint32_t vpn) override { // 페이지 접근
        if (freq.count(vpn)) freq[vpn]++;
    }
    void insert(uint32_t vpn) override { // 페이지 삽입
        if (!freq.count(vpn)) {
            freq[vpn] = 1;
            order.push_back(vpn);
        }
    }
    optional<uint32_t> evict_if_needed() override { // 교체 필요 시
        if ((int)order.size() > capacity) {
            int min_freq = INT_MAX; // INT_INT_MAX -> INT_MAX 수정
            for (uint32_t vpn_in_order : order) {
                min_freq = min(min_freq, freq[vpn_in_order]);
            }

            for (auto it = order.begin(); it != order.end(); ++it) {
                if (freq[*it] == min_freq) {
                    uint32_t victim = *it;
                    freq.erase(victim);
                    order.erase(it);
                    return victim;
                }
            }
        }
        return nullopt;
    }
    void erase(uint32_t vpn) override { // 특정 페이지 제거
        freq.erase(vpn);
        order.remove(vpn);
    }
};

// S3-FIFO 페이지 교체 정책.
// 논문 'FIFO queues are all you need for cache eviction (SOSP 2023)' 기반.
// Q1 (Small FIFO), Q2 (Main FIFO), Q3 (Ghost FIFO) 세 개의 큐를 사용한다.
class S3FIFOReplacement : public ReplacementPolicy {
    list<uint32_t> q1; // Small FIFO (probationary) 큐
    list<uint32_t> q2; // Main FIFO 큐
    list<uint32_t> q3; // Ghost FIFO 큐 (Evict된 원-히트 원더 저장)
    unordered_map<uint32_t, int> freq_map; // 각 VPN 접근 빈도 (0, 1, 2, 3으로 캡핑)
    unordered_set<uint32_t> in_q1, in_q2, in_q3; // 각 큐에 VPN 존재 여부 확인
    
    int cap_q1, cap_q2, cap_q3; // 각 큐 용량
    int total_cap; // 총 캐시 용량

    // 디버깅을 위한 상태 출력.
    void debug_print_state(const std::string& caller_info) {
        if (!debug_log_file.is_open()) return;

        debug_log_file << "[DEBUG - " << caller_info << "] S3FIFO State:" << endl;
        debug_log_file << "  Capacities: Q1=" << cap_q1 << ", Q2=" << cap_q2 << ", Q3=" << cap_q3 << ", Total=" << total_cap << endl;
        debug_log_file << "  Freq Map: {";
        bool first = true;
        for (const auto& pair : freq_map) {
            if (!first) debug_log_file << ", ";
            debug_log_file << "0x" << hex << uppercase << setw(8) << setfill('0') << pair.first << ": " << dec << pair.second;
            first = false;
        }
        debug_log_file << "}" << endl;

        debug_log_file << "  Q1 (Small FIFO, Size: " << q1.size() << ", In_Q1: " << in_q1.size() << "): [";
        first = true;
        for (uint32_t vpn : q1) {
            if (!first) debug_log_file << ", ";
            debug_log_file << "0x" << hex << uppercase << setw(8) << setfill('0') << vpn;
            first = false;
        }
        debug_log_file << "]" << endl;

        debug_log_file << "  Q2 (Main FIFO, Size: " << q2.size() << ", In_Q2: " << in_q2.size() << "): [";
        first = true;
        for (uint32_t vpn : q2) {
            if (!first) debug_log_file << ", ";
            debug_log_file << "0x" << hex << uppercase << setw(8) << setfill('0') << vpn;
            first = false;
        }
        debug_log_file << "]" << endl;

        debug_log_file << "  Q3 (Ghost FIFO, Size: " << q3.size() << ", In_Q3: " << in_q3.size() << "): [";
        first = true;
        for (uint32_t vpn : q3) {
            if (!first) debug_log_file << ", ";
            debug_log_file << "0x" << hex << uppercase << setw(8) << setfill('0') << vpn;
            first = false;
        }
        debug_log_file << "]" << endl;
        debug_log_file << dec;
        debug_log_file.flush();
    }

    // EVICTS 로직 (Small FIFO에서 페이지 처리). 논문 Algorithm 1 EVICTS 함수.
    optional<uint32_t> evictS() {
        if (q1.empty()) return nullopt; // Q1이 비어있으면 교체 불가

        uint32_t t_vpn = q1.back(); 
        q1.pop_back(); 
        in_q1.erase(t_vpn);
        
        string debug_msg_prefix = "Processing Q1 tail: 0x" + to_string(t_vpn) + " (freq: " + to_string(freq_map.count(t_vpn) ? freq_map[t_vpn] : 0) + ")";
        int current_freq = freq_map.count(t_vpn) ? freq_map[t_vpn] : 0; 

        // 논문 Algorithm 1: t.freq == 1이면 G로, t.freq > 1이면 M으로 (freq 초기화)
        if (current_freq == 1) { // freq가 1인 경우 (원-히트 원더) -> Q3 (Ghost)로 이동
            q3.push_front(t_vpn); 
            in_q3.insert(t_vpn);
            freq_map.erase(t_vpn); // Q3로 보내면 freq 정보 삭제

            if ((int)q3.size() > cap_q3) { // Q3 용량 초과 시 가장 오래된 항목 제거
                uint32_t q3_victim = q3.back(); 
                q3.pop_back();
                in_q3.erase(q3_victim);
            }
            debug_print_state(debug_msg_prefix + " -> Evicted (Q1 to Q3)");
            return t_vpn; // 이 페이지가 최종 희생자
        } else if (current_freq >= 2) { // freq가 2 이상인 경우 -> Q2 (Main)의 Head로 이동
            // Q2에 삽입 전 Q2 용량 확보 (evictM 호출)
            while ((int)q2.size() >= cap_q2) { 
                if (optional<uint32_t> m_victim = evictM()) { 
                    debug_print_state(debug_msg_prefix + " -> Triggered EvictM from Q2, victim 0x" + to_string(*m_victim));
                    return m_victim; 
                } else {
                    break; 
                }
            }
            q2.push_front(t_vpn); 
            in_q2.insert(t_vpn);
            freq_map[t_vpn] = 0; // Q1에서 Q2로 이동 시 freq 0으로 초기화
            debug_print_state(debug_msg_prefix + " -> Promoted (Q1 to Q2)");
            return nullopt; // 이 경로에서는 최종 희생자가 나오지 않음
        } else { // current_freq == 0인 경우 (S3-FIFO의 목적상 Q3로 보냄)
            // 논문 Algorithm 1에는 명시되지 않았지만, freq 0인 페이지도 원-히트 원더에 준하여 빠르게 제거.
            q3.push_front(t_vpn); 
            in_q3.insert(t_vpn);
            freq_map.erase(t_vpn); 
            if ((int)q3.size() > cap_q3) {
                uint32_t q3_victim = q3.back(); 
                q3.pop_back();
                in_q3.erase(q3_victim);
            }
            debug_print_state(debug_msg_prefix + " -> Evicted (Q1 to Q3 - freq 0 case)");
            return t_vpn; // 이 페이지가 최종 희생자
        }
    }

    // EVICTM 로직 (Main FIFO에서 페이지 처리). 논문 Algorithm 1 EVICTM 함수.
    optional<uint32_t> evictM() {
        if (q2.empty()) return nullopt; // Q2가 비어있으면 교체 불가

        uint32_t t_vpn = q2.back(); 
        q2.pop_back(); 
        in_q2.erase(t_vpn);

        string debug_msg = "Processing Q2 tail: 0x" + to_string(t_vpn) + " (freq: " + to_string(freq_map.count(t_vpn) ? freq_map[t_vpn] : 0) + ")";
        int current_freq = freq_map.count(t_vpn) ? freq_map[t_vpn] : 0;

        // 논문 Algorithm 1: t.freq > 0 이면 M에 다시 삽입 (freq 감소), 그렇지 않으면 Evict
        if (current_freq > 0) { // freq > 0 이면 Q2 Head로 재삽입
            freq_map[t_vpn]--; // freq 감소
            q2.push_front(t_vpn);
            in_q2.insert(t_vpn);
            debug_print_state(debug_msg + " -> Reinserted to Q2");
            return nullopt; // 최종 희생자가 아님
        } else { // freq == 0 이면 실제 희생자
            freq_map.erase(t_vpn); 
            debug_print_state(debug_msg + " -> Evicted (Q2 to external)");
            return t_vpn; // 이 페이지가 최종 희생자
        }
    }

public:
    // 생성자: 총 용량을 기반으로 각 큐의 용량을 설정한다.
    S3FIFOReplacement(int total) {
        total_cap = total;
        cap_q1 = round(total * 0.1); // 과제 명세: Q1 10% Assignment 3_KR (20250602).pdf]
        cap_q2 = total - cap_q1;     // Q2 90% Assignment 3_KR (20250602).pdf]
        cap_q3 = cap_q1;             // Q3는 Q1과 동일 크기 Assignment 3_KR (20250602).pdf]
        
        // 최소 용량 보장 (total이 작을 경우 0이 되는 것을 방지)
        if (cap_q1 == 0 && total > 0) cap_q1 = 1;
        if (cap_q2 == 0 && total > 0) cap_q2 = 1; 
        if (cap_q3 == 0 && total > 0) cap_q3 = 1;
        
        debug_print_state("Constructor");
    }

    // 페이지 접근 시 호출: 해당 VPN의 빈도를 증가시키고 최대 3으로 캡핑한다.
    void access(uint32_t vpn) override {
        // 논문 Algorithm 1: READ(X) -> x.freq <- min(x.freq+1,3) FIFO Queues are All You Need for Cache Eviction.pdf]
        if (in_q1.count(vpn) || in_q2.count(vpn)) {
            int old_freq = freq_map[vpn]; // freq 변경 전 값 저장 (Lazy Promotion용)
            freq_map[vpn] = min(freq_map[vpn] + 1, 3);
            debug_print_state("Access VPN " + to_string(vpn));

            // Lazy Promotion: Q1에 있던 페이지가 재참조되면 Q2로 지연 승격 Assignment 3_KR (20250602).pdf]
            // (freq가 0에서 1로 바뀌는 순간, 즉 Q1에서 처음 재참조될 때)
            if (in_q1.count(vpn) && old_freq == 0 && freq_map[vpn] == 1) { 
                q1.remove(vpn);
                in_q1.erase(vpn);
                
                // Q2 공간 확보 (evictM 호출) - Q1에서 승격될 때 Q2가 가득 찼으면 EvictM 발생
                while ((int)q2.size() >= cap_q2) { 
                    if (optional<uint32_t> m_victim = evictM()) { 
                        debug_log_file << "[DEBUG - Lazy Promotion Triggered EvictM, victim 0x" << hex << uppercase << *m_victim << dec << "]" << endl;
                        debug_log_file.flush();
                        // evictM이 희생자를 반환하면, 그 희생자가 최종 희생자.
                        // 하지만 access 함수에서는 희생자를 반환할 수 없으므로, 이 시뮬레이션에서는 이 희생자는 그냥 제거된다.
                    } else {
                        break; 
                    }
                }
                
                q2.push_front(vpn);
                in_q2.insert(vpn);
                freq_map[vpn] = 0; // Q2로 승격 시 freq 0으로 초기화
                debug_print_state("Lazy Promotion: VPN " + to_string(vpn) + " (Q1 -> Q2)");
            }
        }
    }

    // 페이지 삽입 시 호출: 캐시 미스 발생 시 호출된다.
    // 논문 Algorithm 1: INSERT(X) 로직 FIFO Queues are All You Need for Cache Eviction.pdf]
    void insert(uint32_t vpn) override {
        // 1. 이미 캐시에 있는지 확인 (캐시 히트 시 삽입 스킵)
        if (in_q1.count(vpn) || in_q2.count(vpn)) {
            debug_log_file << "[DEBUG - Insert (Already in Q1/Q2) VPN 0x" << hex << uppercase << vpn << dec << "] S3FIFO State: Skipping insertion." << endl;
            debug_log_file.flush();
            return;
        }

        // 2. 논문 Algorithm 1의 `while cache is full do evict()` 부분은 `handle_page_fault`에서 `evict_if_needed()` 호출로 처리된다.
        //    따라서 `insert` 함수 자체에서는 선제적인 `evictS/M` 호출을 하지 않는다.

        // 3. 실제 삽입 진행
        // 논문 INSERT(X): "if x in G then insert x to head of M else insert x to head of S" FIFO Queues are All You Need for Cache Eviction.pdf]
        if (in_q3.count(vpn)) { // G에 있는 경우 G->M
            debug_log_file << "[DEBUG - Insert (From Q3 to Q2) VPN 0x" << hex << uppercase << vpn << dec << "]" << endl;
            debug_log_file.flush();
            q3.remove(vpn); 
            in_q3.erase(vpn);
            q2.push_front(vpn); 
            in_q2.insert(vpn);
            freq_map[vpn] = 0; // x.freq <- 0 FIFO Queues are All You Need for Cache Eviction.pdf]
            debug_print_state("Insert (From Q3 to Q2) VPN " + to_string(vpn)); 
        } else { // G에 없으면 S-FIFO로 삽입
            debug_log_file << "[DEBUG - Insert (To Q1 - New Object) VPN 0x" << hex << uppercase << vpn << dec << "]" << endl;
            debug_log_file.flush();
            q1.push_front(vpn); 
            in_q1.insert(vpn);
            freq_map[vpn] = 0; 
            debug_print_state("Insert (To Q1 - New Object) VPN " + to_string(vpn)); 
        }
    }

    // 캐시 용량 관리. `handle_page_fault`에서 호출되어 총 캐시 용량을 맞춘다.
    // 논문 Algorithm 1: EVICT 함수 로직을 반복적으로 호출하여 희생자를 찾는다. FIFO Queues are All You Need for Cache Eviction.pdf]
    optional<uint32_t> evict_if_needed() override {
        debug_print_state("Evict_if_needed Start (Current Cache Size: " + to_string(q1.size() + q2.size()) + ")");

        // 총 캐시 (Q1+Q2) 용량이 total_cap과 같거나 초과하는 동안 반복적으로 교체를 시도한다.
        while ((int)(q1.size() + q2.size()) >= total_cap) { // '=' 포함 (가득 찼을 때도 교체)
            optional<uint32_t> victim_candidate = nullopt;

            // 논문 Algorithm 1 EVICT: if S.size >= 0.1 cache size then evictS() else evictM() FIFO Queues are All You Need for Cache Eviction.pdf]
            if ((int)q1.size() >= cap_q1 && !q1.empty()) { // Q1이 비어있지 않고, 임계값 이상이면 evictS
                victim_candidate = evictS();
            } else if (!q2.empty()) { // Q1 조건 불만족 시 Q2가 비어있지 않으면 evictM
                victim_candidate = evictM();
            } else {
                // Q1, Q2 모두 비어있거나 교체 불가능한 논리적 오류 상황. 이 과제에서는 발생하지 않아야 한다.
                debug_log_file << "[DEBUG - Evict_if_needed Error: Both Q1 and Q2 are empty or cannot evict, but cache size still exceeds capacity." << endl;
                debug_log_file.flush();
                return nullopt; 
            }

            if (victim_candidate.has_value()) {
                debug_print_state("Evict_if_needed End (Victim Found: 0x" + to_string(*victim_candidate) + ")");
                return victim_candidate; // 최종 희생자 반환
            }
            // victim_candidate가 nullopt 이면 (내부 이동만 발생한 경우),
            // total_cap을 만족할 때까지 루프를 계속 돌며 다시 교체 시도한다.
        }
        debug_print_state("Evict_if_needed End (Capacity satisfied, no victim)");
        return nullopt; // 캐시 용량 조건을 만족하면 종료
    }
    
    // 특정 페이지 제거: 모든 큐와 빈도 맵에서 해당 페이지를 제거한다.
    void erase(uint32_t vpn) override {
        if (in_q1.count(vpn)) {
            q1.remove(vpn);
            in_q1.erase(vpn);
        }
        if (in_q2.count(vpn)) {
            q2.remove(vpn);
            in_q2.erase(vpn);
        }
        if (in_q3.count(vpn)) {
            q3.remove(vpn);
            in_q3.erase(vpn);
        }
        freq_map.erase(vpn); 
    }
};

// 전역 TLB 및 페이지 정책 스마트 포인터.
unique_ptr<ReplacementPolicy> tlb_policy;
unique_ptr<ReplacementPolicy> page_policy;

// TLB 조회. 히트 시 PFN 반환 및 TLB 정책 access 호출.
bool tlb_lookup(uint32_t vpn, int& pfn) {
    for (auto it = tlb.begin(); it != tlb.end(); ++it) {
        if (it->valid && it->vpn == vpn) {
            pfn = it->pfn;
            tlb_policy->access(vpn); // TLB 히트 시 TLB 정책 access 호출
            return true;
        }
    }
    return false;
}

// TLB 갱신. TLB 미스 시 호출.
void tlb_update(uint32_t vpn, int pfn) {
    // 1. TLB에 이미 해당 VPN이 있는지 확인 (TLB 히트 시)
    for (auto it = tlb.begin(); it != tlb.end(); ++it) {
        if (it->vpn == vpn) {
            it->pfn = pfn;
            tlb_policy->access(vpn);
            return;
        }
    }
    
    // 2. TLB 미스 시: 새 항목 삽입
    tlb_policy->insert(vpn);

    // 3. TLB 용량 초과 시 희생자 제거
    if (auto victim_vpn = tlb_policy->evict_if_needed()) {
        tlb.remove_if([&](const TLBEntry& e) { return e.vpn == *victim_vpn; });
    }

    // 4. 새 엔트리를 TLB에 추가
    tlb.push_back({vpn, pfn, true});
}

// vpn_map: 가상 페이지 번호(VPN)로부터 물리 프레임 번호(PFN) 및
// 페이지 디렉토리 인덱스(PDI), 페이지 테이블 인덱스(PTI)를 찾기 위한 매핑.
unordered_map<uint32_t, pair<int, pair<int, int>>> vpn_map;
// 사용 가능한 물리 프레임 번호 큐.
queue<int> free_pfn_pool;

// TLB 항목 일관성을 위해 특정 VPN에 대한 TLB 엔트리 무효화.
void tlb_invalidate(uint32_t vpn) {
    tlb.remove_if([&](const TLBEntry& e) { return e.vpn == vpn; });
    tlb_policy->erase(vpn); // TLB 정책에서도 해당 VPN 제거
}

// 페이지 부재(Page Fault) 처리.
EvictionResultInfo handle_page_fault(uint32_t vpn, int pdi, int pti, int& assigned_pfn) {
    EvictionResultInfo result = {nullopt, nullopt};

    // 1. 페이지 정책에 새 페이지 삽입 알림
    page_policy->insert(vpn);

    // 2. 물리 프레임이 가득 찼다면 페이지 교체를 수행하여 희생자 결정
    optional<uint32_t> evicted_vpn_opt = page_policy->evict_if_needed();

    // 3. 희생자 페이지가 있다면 시스템에서 제거
    if (evicted_vpn_opt.has_value()) {
        uint32_t victim_vpn = evicted_vpn_opt.value();
        result.vpn = victim_vpn;
        result.va = victim_vpn << 12;

        if (vpn_map.count(victim_vpn)) {
            auto [old_pfn, indices] = vpn_map[victim_vpn];

            page_directory[indices.first][indices.second].valid = false; // 페이지 테이블 엔트리 무효화
            vpn_map.erase(victim_vpn); // vpn_map에서 매핑 제거
            tlb_invalidate(victim_vpn); // TLB에서 해당 VPN 무효화
            free_pfn_pool.push(old_pfn); // 물리 프레임 재사용 위해 반환

            // S3FIFO는 evict_if_needed 내부에서 이미 처리하므로 추가 erase 불필요.
            if (dynamic_cast<S3FIFOReplacement*>(page_policy.get()) == nullptr) {
                 page_policy->erase(victim_vpn);
            }
        }
    }

    // 4. 새 페이지를 위한 물리 프레임 할당
    if (!free_pfn_pool.empty()) {
        assigned_pfn = free_pfn_pool.front(); free_pfn_pool.pop();
    } else {
        if (next_free_pfn < TOTAL_FRAMES) {
            assigned_pfn = next_free_pfn++;
        } else {
            debug_log_file << "[ERROR] No free PFN available and TOTAL_FRAMES exceeded." << endl;
            exit(1);
        }
    }

    // 5. 페이지 테이블 및 VPN 매핑 갱신
    page_directory[pdi][pti] = {assigned_pfn, true};
    vpn_map[vpn] = {assigned_pfn, {pdi, pti}};

    return result;
}

// 가상 주소(va)를 물리 주소로 변환하고 시뮬레이션 결과를 출력한다.
void translate(uint32_t va) {
    total_refs++;
    
    int pdi = (va >> 22) & 0x3FF;
    int pti = (va >> 12) & 0x3FF;
    int offset = va & 0xFFF;
    uint32_t vpn = va >> 12;
    
    int pfn;
    string tlb_result, page_fault_result, evict_info;

    // 1. TLB 조회
    if (tlb_lookup(vpn, pfn)) {
        tlb_hits++;
        tlb_result = "TLB hit";
        page_fault_result = "No page fault";
        page_policy->access(vpn); // TLB 히트는 곧 페이지 테이블 히트이므로 페이지 정책에 접근 알림
    } else {
        tlb_misses++;
        tlb_result = "TLB miss";
        
        // 2. 페이지 테이블 조회 (TLB 미스 시)
        PageTableEntry& entry = page_directory[pdi][pti];

        if (!entry.valid) { // 페이지 부재 발생
            page_faults++;
            int assigned_pfn;
            EvictionResultInfo evicted = handle_page_fault(vpn, pdi, pti, assigned_pfn);
            pfn = assigned_pfn;

            page_fault_result = "Page fault";
            if (evicted.va.has_value()) { // 교체된 페이지 정보가 있다면 출력 문자열에 추가
                stringstream ss;
                ss << "Evicted 0x" << uppercase << hex << setw(8) << setfill('0') << evicted.va.value();
                evict_info = ss.str();
            }
        } else { // 페이지 테이블 히트
            pfn = entry.pfn;
            page_fault_result = "No page fault";
            page_policy->access(vpn); // 페이지 테이블 히트이므로 페이지 정책에 접근 알림
        }
        // 3. TLB 갱신 (TLB 미스 후 PFN을 찾거나 할당했을 때)
        tlb_update(vpn, pfn);
    }

    // 물리 주소(PA) 계산
    uint32_t pa = (static_cast<uint32_t>(pfn) << 12) | offset;
    
    // 결과 출력
    cout << "0x" << setw(8) << setfill('0') << hex << uppercase << va
         << " -> 0x" << setw(8) << setfill('0') << hex << uppercase << pa
         << ", " << tlb_result << ", " << page_fault_result;
    if (!evict_info.empty()) {
        cout << ", " << evict_info;
    }
    cout << endl;
}

// 최종 통계 요약을 출력한다.
void print_summary() {
    cout << std::dec;
    cout << "Total references: " << total_refs << endl;
    cout << "TLB hits: " << tlb_hits << endl;
    cout << "TLB misses: " << tlb_misses << endl;
    cout << fixed << setprecision(1);
    cout << "TLB hit ratio: " << (total_refs == 0 ? 0.0 : 100.0 * tlb_hits / total_refs) << "%" << endl;
    cout << "Page faults: " << page_faults << endl;
    cout << "Page fault rate: " << (total_refs == 0 ? 0.0 : 100.0 * page_faults / total_refs) << "%" << endl;
}

int main(int argc, char* argv[]) {
    const char* log_dir = "log";
    struct stat st = {0};
    if (stat(log_dir, &st) == -1) {
        mkdir(log_dir, 0777);
    }

    auto now = chrono::system_clock::now();
    time_t now_c = chrono::system_clock::to_time_t(now);
    tm* local_tm = localtime(&now_c);

    stringstream ss;
    ss << log_dir << "/debug_";
    ss << put_time(local_tm, "%Y%m%d_%H%M%S");
    ss << ".log";
    string filename = ss.str();

    if (argc < 4) {
        cerr << "Usage: ./vmsim [total_frames] [tlb_size] [policy]" << endl;
        return 1;
    }
    
    TOTAL_FRAMES = stoi(argv[1]);
    TLB_SIZE = stoi(argv[2]);
    string policy = argv[3];

    for (int i = 0; i < TOTAL_FRAMES; ++i) {
        free_pfn_pool.push(i);
    }

    if (policy == "FIFO") {
        tlb_policy = make_unique<FIFOReplacement>(TLB_SIZE);
        page_policy = make_unique<FIFOReplacement>(TOTAL_FRAMES);
    } else if (policy == "LRU") {
        tlb_policy = make_unique<LRUReplacement>(TLB_SIZE);
        page_policy = make_unique<LRUReplacement>(TOTAL_FRAMES);
    } else if (policy == "LFU") {
        tlb_policy = make_unique<LFUReplacement>(TLB_SIZE);
        page_policy = make_unique<LFUReplacement>(TOTAL_FRAMES);
    } else if (policy == "S3FIFO") {
        debug_log_file.open(filename);
        if (!debug_log_file.is_open()) {
            cerr << "Error: Could not open " << filename << " for writing." << endl;
            return 1;
        }

        tlb_policy = make_unique<S3FIFOReplacement>(TLB_SIZE);
        page_policy = make_unique<S3FIFOReplacement>(TOTAL_FRAMES);
    } else {
        cerr << "Unsupported policy. Use FIFO, LRU, LFU, or S3FIFO." << endl;
        return 1;
    }

    string line;
    while (getline(cin, line)) {
        if (line.empty()) continue;
        uint32_t va;
        stringstream ss(line);
        ss >> hex >> va;
        translate(va);
    }

    print_summary();
    
    if (policy == "S3FIFO" && debug_log_file.is_open()) {
        debug_log_file.close();
    }

    return 0;
}