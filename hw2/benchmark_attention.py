import subprocess
import numpy as np
import matplotlib.pyplot as plt
import pandas as pd
from tqdm import tqdm
import os
import tempfile
import seaborn as sns
sns.set(style="whitegrid")

# 설정
ATTENTION_EXEC = "./attention"  # 실행 파일 경로
TRIALS = 3  # 반복 횟수

# 입력 생성
def generate_input(R, C, D):
    Q = np.random.randint(0, 10, size=(R, C))
    K = np.random.randint(0, 10, size=(R, C))
    V = np.random.randint(0, 10, size=(R, D))
    
    input_str = f"{R} {C}\n"
    input_str += "\n".join(" ".join(map(str, row)) for row in Q) + "\n"
    input_str += f"{R} {C}\n"
    input_str += "\n".join(" ".join(map(str, row)) for row in K) + "\n"
    input_str += f"{R} {D}\n"
    input_str += "\n".join(" ".join(map(str, row)) for row in V) + "\n"
    return input_str

# 실행 함수
def run_attention(input_data: str, threads: int):
    with tempfile.NamedTemporaryFile(delete=False, mode="w") as tmpfile:
        tmpfile.write(input_data)
        tmpfile_path = tmpfile.name

    cmd = [ATTENTION_EXEC, str(threads)]
    with open(tmpfile_path, "r") as f:
        result = subprocess.run(cmd, stdin=f, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    
    os.unlink(tmpfile_path)

    try:
        latency = int(result.stdout.splitlines()[0])
    except:
        latency = -1
    return latency

# 실험 1: 스레드 수 변화
def thread_experiment(R=200, C=200, D=200):
    results = []
    input_data = generate_input(R, C, D)

    for thread_num in tqdm(range(1, 11), desc="Thread Test"):
        times = [run_attention(input_data, thread_num) for _ in range(TRIALS)]
        avg_time = np.mean(times)
        results.append((thread_num, avg_time))
    
    return pd.DataFrame(results, columns=["Threads", "Latency (ms)"])

# 실험 2: 행렬 크기 변화
def size_experiment(threads=4, sizes=range(100, 1100, 100)):
    results = []

    for size in tqdm(sizes, desc="Size Test"):
        input_data = generate_input(size, size, size // 2)
        times = [run_attention(input_data, threads) for _ in range(TRIALS)]
        avg_time = np.mean(times)
        results.append((size, avg_time))
    
    return pd.DataFrame(results, columns=["Size (R=C)", "Latency (ms)"])

# 그래프 저장
def save_thread_latency_plot(df, filename="thread_vs_latency.png"):
    plt.figure(figsize=(7, 5))
    sns.lineplot(data=df, x="Threads", y="Latency (ms)", marker="o")
    plt.title("Latency vs Threads (Fixed Size: 200x200)")
    plt.xlabel("Threads")
    plt.ylabel("Latency (ms)")
    plt.grid(True)
    plt.tight_layout()
    plt.savefig(filename)
    plt.close()

def save_size_latency_plot(df, filename="size_vs_latency.png"):
    plt.figure(figsize=(7, 5))
    sns.lineplot(data=df, x="Size (R=C)", y="Latency (ms)", marker="o", color="green")
    plt.title("Latency vs Matrix Size (Fixed Threads: 4)")
    plt.xlabel("Matrix Size (R=C)")
    plt.ylabel("Latency (ms)")
    plt.grid(True)
    plt.tight_layout()
    plt.savefig(filename)
    plt.close()

# 표 이미지 저장 (각각 따로)
def save_latency_table_image_separately(df1, df2, filename1="latency_table_threads.png", filename2="latency_table_sizes.png"):
    # Thread Table
    fig1, ax1 = plt.subplots(figsize=(5, 3))
    ax1.axis("off")
    table1 = ax1.table(cellText=df1.round(2).values,
                      colLabels=df1.columns,
                      loc="center",
                      cellLoc='center')
    table1.scale(1.2, 1.5)
    ax1.set_title("Latency by Threads", pad=20, fontsize=12)
    plt.tight_layout()
    plt.savefig(filename1)
    plt.close()

    # Size Table
    fig2, ax2 = plt.subplots(figsize=(5, 3))
    ax2.axis("off")
    table2 = ax2.table(cellText=df2.round(2).values,
                      colLabels=df2.columns,
                      loc="center",
                      cellLoc='center')
    table2.scale(1.2, 1.5)
    ax2.set_title("Latency by Size", pad=20, fontsize=12)
    plt.tight_layout()
    plt.savefig(filename2)
    plt.close()

# 실행
if __name__ == "__main__":
    df_threads = thread_experiment()
    df_sizes = size_experiment()

    print("\n[Thread 수에 따른 성능 분석 결과]")
    print(df_threads)
    print("\n[행렬 크기에 따른 성능 분석 결과]")
    print(df_sizes)

    save_thread_latency_plot(df_threads, "thread_vs_latency.png")
    save_size_latency_plot(df_sizes, "size_vs_latency.png")
    save_latency_table_image_separately(df_threads, df_sizes,
                                        filename1="latency_table_threads.png",
                                        filename2="latency_table_sizes.png")

    print("\n✅ 이미지 저장 완료:")
    print(" - thread_vs_latency.png")
    print(" - size_vs_latency.png")
    print(" - latency_table_threads.png")
    print(" - latency_table_sizes.png")
