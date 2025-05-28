import subprocess
import numpy as np
import matplotlib.pyplot as plt
import pandas as pd
from tqdm import tqdm
import os
import tempfile
import seaborn as sns
sns.set(style="whitegrid")

# 실행 파일 경로
MULTI_EXEC = "./multiHeadAttention"
TRIALS = 3  # 반복 횟수

# 입력 생성 (H개의 Q, K, V)
def generate_multi_input(H, R, C, D):
    input_str = f"{H}\n"
    for _ in range(H):
        Q = np.random.randint(0, 10, size=(R, C))
        K = np.random.randint(0, 10, size=(R, C))
        V = np.random.randint(0, 10, size=(R, D))
        input_str += f"{R} {C}\n"
        input_str += "\n".join(" ".join(map(str, row)) for row in Q) + "\n"
        input_str += f"{R} {C}\n"
        input_str += "\n".join(" ".join(map(str, row)) for row in K) + "\n"
        input_str += f"{R} {D}\n"
        input_str += "\n".join(" ".join(map(str, row)) for row in V) + "\n"
    return input_str

# 실행 함수
def run_multi_attention(input_data: str, num_processes: int):
    with tempfile.NamedTemporaryFile(delete=False, mode="w") as tmpfile:
        tmpfile.write(input_data)
        tmpfile_path = tmpfile.name

    cmd = [MULTI_EXEC, str(num_processes)]
    with open(tmpfile_path, "r") as f:
        result = subprocess.run(cmd, stdin=f, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    os.unlink(tmpfile_path)

    try:
        latency = int(result.stdout.splitlines()[0])
    except:
        latency = -1
    return latency

# 실험 1: 프로세스 수 변화
def process_experiment(R=200, C=200, D=200):
    results = []
    for H in tqdm(range(1, 11), desc="Process Count Test"):
        input_data = generate_multi_input(H, R, C, D)
        times = [run_multi_attention(input_data, H) for _ in range(TRIALS)]
        avg_time = np.mean(times)
        results.append((H, avg_time))
    return pd.DataFrame(results, columns=["Processes", "Latency (ms)"])

# 실험 2: 입력 크기 변화
def multi_size_experiment(H=4, sizes=range(100, 1100, 100)):
    results = []
    for size in tqdm(sizes, desc="Size Test"):
        input_data = generate_multi_input(H, size, size, size // 2)
        times = [run_multi_attention(input_data, H) for _ in range(TRIALS)]
        avg_time = np.mean(times)
        results.append((size, avg_time))
    return pd.DataFrame(results, columns=["Size (R=C)", "Latency (ms)"])

# 시각화 함수
def save_multi_graphs_and_tables(df_proc, df_size):
    # 그래프 1: 프로세스 수 vs 시간
    plt.figure(figsize=(7, 5))
    sns.lineplot(data=df_proc, x="Processes", y="Latency (ms)", marker="o")
    plt.title("Latency vs Processes (Fixed Size: 200x200)")
    plt.xlabel("Processes")
    plt.ylabel("Latency (ms)")
    plt.grid(True)
    plt.tight_layout()
    plt.savefig("multi_process_vs_latency.png")
    plt.close()

    # 그래프 2: 입력 크기 vs 시간 (초록)
    plt.figure(figsize=(7, 5))
    sns.lineplot(data=df_size, x="Size (R=C)", y="Latency (ms)", marker="o", color="green")
    plt.title("Latency vs Matrix Size (Fixed Processes: 4)")
    plt.xlabel("Matrix Size (R=C)")
    plt.ylabel("Latency (ms)")
    plt.grid(True)
    plt.tight_layout()
    plt.savefig("multi_size_vs_latency.png")
    plt.close()

    # 표 1: 프로세스 수
    fig1, ax1 = plt.subplots(figsize=(5, 3))
    ax1.axis("off")
    table1 = ax1.table(cellText=df_proc.round(2).values,
                      colLabels=df_proc.columns,
                      loc="center",
                      cellLoc='center')
    table1.scale(1.2, 1.5)
    ax1.set_title("Latency by Processes", pad=20, fontsize=12)
    plt.tight_layout()
    plt.savefig("multi_latency_table_processes.png")
    plt.close()

    # 표 2: 크기
    fig2, ax2 = plt.subplots(figsize=(5, 3))
    ax2.axis("off")
    table2 = ax2.table(cellText=df_size.round(2).values,
                      colLabels=df_size.columns,
                      loc="center",
                      cellLoc='center')
    table2.scale(1.2, 1.5)
    ax2.set_title("Latency by Size (Multi)", pad=20, fontsize=12)
    plt.tight_layout()
    plt.savefig("multi_latency_table_sizes.png")
    plt.close()

# 메인 실행
if __name__ == "__main__":
    df_proc = process_experiment()
    df_size = multi_size_experiment()

    print("\n[프로세스 수에 따른 성능 분석 결과]")
    print(df_proc)

    print("\n[입력 크기에 따른 성능 분석 결과]")
    print(df_size)

    save_multi_graphs_and_tables(df_proc, df_size)

    print("\n✅ 이미지 저장 완료:")
    print(" - multi_process_vs_latency.png")
    print(" - multi_size_vs_latency.png")
    print(" - multi_latency_table_processes.png")
    print(" - multi_latency_table_sizes.png")
