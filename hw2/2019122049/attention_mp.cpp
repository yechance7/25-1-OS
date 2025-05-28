#include <iostream>
#include <vector>
#include <chrono>
#include <cstdlib>
#include <pthread.h>
using namespace std;

int Rq, C, Rk, D;
vector<vector<int>> Q, K, V, result;

struct ThreadArg {
    int start_row, end_row;
};

void* compute_attention(void* arg) {
    ThreadArg* t = (ThreadArg*)arg;
    for (int i = t->start_row; i < t->end_row; ++i) {
        for (int j = 0; j < Rk; ++j) {
            int dot = 0;
            for (int k = 0; k < C; ++k) dot += Q[i][k] * K[j][k];
            for (int d = 0; d < D; ++d) result[i][d] += dot * V[j][d];
        }
    }
    return nullptr;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: ./attention_mp [head_index]" << endl;
        return 1;
    }

    int head_idx = atoi(argv[1]);

    int H;
    cin >> H;
    if (head_idx < 0 || head_idx >= H) {
        cerr << "Invalid head index" << endl;
        return 1;
    }

    // 모든 head를 순회하되, 지정된 head만 저장
    for (int h = 0; h < H; ++h) {
        int r, c, d;
        vector<vector<int>> tmpQ, tmpK, tmpV;

        cin >> r >> c;
        tmpQ.assign(r, vector<int>(c));
        for (auto& row : tmpQ) for (int& x : row) cin >> x;

        cin >> r >> c;
        tmpK.assign(r, vector<int>(c));
        for (auto& row : tmpK) for (int& x : row) cin >> x;

        cin >> r >> d;
        tmpV.assign(r, vector<int>(d));
        for (auto& row : tmpV) for (int& x : row) cin >> x;

        // 해당 head의 행렬만 사용
        if (h == head_idx) {
            Q = tmpQ; K = tmpK; V = tmpV;
            Rq = Q.size(); Rk = K.size(); C = Q[0].size(); D = V[0].size();
        }
    }

    result.assign(Rq, vector<int>(D, 0));

    auto start = chrono::high_resolution_clock::now();

    int thread_num = 4;  // 고정 스레드 수
    vector<pthread_t> threads(thread_num);
    vector<ThreadArg> args(thread_num);

    int rows_per_thread = Rq / thread_num;
    int rem = Rq % thread_num, curr = 0;

    for (int i = 0; i < thread_num; ++i) {
        args[i].start_row = curr;
        args[i].end_row = curr + rows_per_thread + (i < rem ? 1 : 0);
        curr = args[i].end_row;
        pthread_create(&threads[i], nullptr, compute_attention, &args[i]);
    }
    for (auto& t : threads) pthread_join(t, nullptr);

    auto end = chrono::high_resolution_clock::now();
    int latency = chrono::duration_cast<chrono::milliseconds>(end - start).count();

    // 출력: latency + attention 결과
    cout << latency << endl;
    for (auto& row : result) {
        for (int x : row) cout << x << ' ';
        cout << '\n';
    }

    return 0;
}
