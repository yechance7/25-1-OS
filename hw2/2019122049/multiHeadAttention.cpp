#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <cstring>
#include <chrono>

using namespace std;

int Rq, Rk, C, D;

// 공유 메모리로부터 결과를 누적
void add_to_result(int* shm_ptr, vector<vector<int>>& result) {
    for (int i = 0; i < Rq; ++i)
        for (int j = 0; j < D; ++j)
            result[i][j] += shm_ptr[i * D + j];
}

int main() {
    int H;
    cin >> H;

    stringstream full_input;
    full_input << H << "\n";  // head 수 포함 전체 입력을 저장

    // 모든 head 입력 읽기 & 버퍼 저장
    for (int h = 0; h < H; ++h) {
        cin >> Rq >> C;
        full_input << Rq << " " << C << "\n";
        for (int i = 0, x; i < Rq * C; ++i) {
            cin >> x; full_input << x << " ";
        }
        full_input << "\n";

        cin >> Rk >> C;
        full_input << Rk << " " << C << "\n";
        for (int i = 0, x; i < Rk * C; ++i) {
            cin >> x; full_input << x << " ";
        }
        full_input << "\n";

        cin >> Rk >> D;
        full_input << Rk << " " << D << "\n";
        for (int i = 0, x; i < Rk * D; ++i) {
            cin >> x; full_input << x << " ";
        }
        full_input << "\n";
    }

    string all_input = full_input.str();
    int matrix_size = Rq * D;
    int shm_total_size = H * matrix_size * sizeof(int);

    // 공유 메모리 설정
    int* shm_base = (int*) mmap(nullptr, shm_total_size, PROT_READ | PROT_WRITE,
                                MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    memset(shm_base, 0, shm_total_size);

    auto start = chrono::high_resolution_clock::now();
    vector<pid_t> pids(H);

    for (int h = 0; h < H; ++h) {
        int pipefd[2];
        (void)pipe(pipefd);

        pid_t pid = fork();
        if (pid == 0) {
            // 자식: 입력 파이프 연결
            dup2(pipefd[0], STDIN_FILENO);
            close(pipefd[1]);

            int outpipe[2];
            (void)pipe(outpipe);
            pid_t grandchild = fork();

            if (grandchild == 0) {
                // 손자: stdout 파이프 연결 후 exec
                dup2(outpipe[1], STDOUT_FILENO);
                close(outpipe[0]);
                execlp("./attention_mp", "./attention_mp", to_string(h).c_str(), nullptr);
                perror("exec failed");
                exit(1);
            } else {
                // 자식: 손자 결과 수신 후 공유 메모리에 저장
                close(outpipe[1]);
                FILE* f = fdopen(outpipe[0], "r");
                int dummy_latency;
                (void)fscanf(f, "%d", &dummy_latency);
                int* shm_ptr = shm_base + h * matrix_size;
                for (int i = 0; i < matrix_size; ++i) {
                    (void)fscanf(f, "%d", &shm_ptr[i]);
                }
                fclose(f);
                waitpid(grandchild, nullptr, 0);
                exit(0);
            }
        } else {
            // 부모: 자식에게 입력 전달
            close(pipefd[0]);
            (void)write(pipefd[1], all_input.c_str(), all_input.size());
            close(pipefd[1]);
            pids[h] = pid;
        }
    }

    // 모든 자식 종료 대기
    for (pid_t pid : pids)
        waitpid(pid, nullptr, 0);

    auto end = chrono::high_resolution_clock::now();
    int latency = chrono::duration_cast<chrono::milliseconds>(end - start).count();

    // 최종 결과 합산 및 출력
    vector<vector<int>> result(Rq, vector<int>(D, 0));
    for (int h = 0; h < H; ++h) {
        int* shm_ptr = shm_base + h * matrix_size;
        add_to_result(shm_ptr, result);
    }

    cout << latency << endl;
    for (auto& row : result) {
        for (int x : row) cout << x << " ";
        cout << "\n";
    }

    return 0;
}
