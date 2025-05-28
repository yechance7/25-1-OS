#include <iostream>
#include <vector>
#include <chrono>
#include <pthread.h>
using namespace std;

// 전역 변수: 행렬 크기 및 결과 저장
int Rq, C, Rk, D;
vector<vector<int>> Q, K, V, result;

// 스레드별로 연산할 row 구간 정의
struct ThreadArg {
    int start_row, end_row;
};

// 각 스레드에서 실행될 함수 - attention 연산
void* compute_attention(void* arg) {
    ThreadArg* t = (ThreadArg*)arg;
    for (int i = t->start_row; i < t->end_row; ++i) {
        for (int j = 0; j < Rk; ++j) {
            int dot = 0;
            for (int k = 0; k < C; ++k) {
                dot += Q[i][k] * K[j][k];  // Q * Kᵀ 연산
            }
            for (int d = 0; d < D; ++d) {
                result[i][d] += dot * V[j][d];  // 곱한 결과에 V까지 곱해주기
            }
        }
    }
    return nullptr;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: ./attention [total_thread_num]" << endl;
        return 1;
    }

    int total_thread_num = atoi(argv[1]);

    // Q 입력
    cin >> Rq >> C;
    Q.assign(Rq, vector<int>(C));
    for (auto& row : Q) for (int& x : row) cin >> x;

    // K 입력
    cin >> Rk >> C;
    K.assign(Rk, vector<int>(C));
    for (auto& row : K) for (int& x : row) cin >> x;

    // V 입력
    cin >> Rk >> D;
    V.assign(Rk, vector<int>(D));
    for (auto& row : V) for (int& x : row) cin >> x;

    result.assign(Rq, vector<int>(D, 0));  // 결과 행렬 초기화

    auto start = chrono::high_resolution_clock::now();  // 시간 측정 시작

    // 스레드 생성 및 분할
    vector<pthread_t> threads(total_thread_num);
    vector<ThreadArg> args(total_thread_num);

    int rows_per_thread = Rq / total_thread_num;
    int remainder = Rq % total_thread_num;

    int curr = 0;
    for (int i = 0; i < total_thread_num; ++i) {
        args[i].start_row = curr;
        args[i].end_row = curr + rows_per_thread + (i < remainder ? 1 : 0);
        curr = args[i].end_row;
        pthread_create(&threads[i], nullptr, compute_attention, &args[i]);
    }

    // 모든 스레드 종료 대기
    for (int i = 0; i < total_thread_num; ++i) {
        pthread_join(threads[i], nullptr);
    }

    auto end = chrono::high_resolution_clock::now();
    int latency = chrono::duration_cast<chrono::milliseconds>(end - start).count();

    // 출력 형식: 시간 + 결과 행렬
    cout << latency << endl;
    for (auto& row : result) {
        for (int x : row) cout << x << ' ';
        cout << '\n';
    }

    return 0;
}
