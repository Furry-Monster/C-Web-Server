import socket
import time
import concurrent.futures
import statistics
import random
import json
from datetime import datetime

# 定义不同的请求类型
REQUESTS = [
    {"path": "/", "method": "GET", "description": "首页"},
    {"path": "/cat.jpg", "method": "GET", "description": "静态图片"},
    {"path": "/rubbish.txt", "method": "GET", "description": "文本文件"},
    {"path": "/nonexistent", "method": "GET", "description": "404页面"},
]


def format_http_request(method, path, headers=None):
    if headers is None:
        headers = {}

    request = f"{method} {path} HTTP/1.1\r\n"
    request += "Host: localhost:3490\r\n"
    for key, value in headers.items():
        request += f"{key}: {value}\r\n"
    request += "\r\n"
    return request.encode()


def make_request(request_type):
    start = time.time()
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5.0)  # 5秒超时
        sock.connect(("localhost", 3490))

        request = format_http_request(request_type["method"], request_type["path"])

        sock.send(request)
        response = b""
        while True:
            data = sock.recv(4096)
            if not data:
                break
            response += data

        sock.close()
        end = time.time()
        return {
            "time": end - start,
            "size": len(response),
            "path": request_type["path"],
            "success": True,
        }
    except Exception as e:
        return {
            "time": time.time() - start,
            "error": str(e),
            "path": request_type["path"],
            "success": False,
        }


def run_load_test(total_requests=1000, concurrent_users=50, duration=60):
    results = []
    start_time = time.time()
    requests_made = 0

    print(
        f"开始压力测试 - 目标请求数: {total_requests}, 并发用户数: {concurrent_users}"
    )

    with concurrent.futures.ThreadPoolExecutor(
        max_workers=concurrent_users
    ) as executor:
        futures = []

        while requests_made < total_requests and (time.time() - start_time) < duration:
            # 随机选择一个请求类型
            request_type = random.choice(REQUESTS)
            futures.append(executor.submit(make_request, request_type))
            requests_made += 1

        for future in concurrent.futures.as_completed(futures):
            results.append(future.result())

    # 分析结果
    successful_requests = [r for r in results if r["success"]]
    failed_requests = [r for r in results if not r["success"]]

    # 按请求路径分组统计
    path_stats = {}
    for path in set(r["path"] for r in results):
        path_requests = [r for r in results if r["path"] == path]
        successful_path_requests = [r for r in path_requests if r["success"]]

        if successful_path_requests:
            response_times = [r["time"] for r in successful_path_requests]
            path_stats[path] = {
                "总请求数": len(path_requests),
                "成功请求数": len(successful_path_requests),
                "成功率": (len(successful_path_requests) / len(path_requests)) * 100,
                "平均响应时间": statistics.mean(response_times),
                "最大响应时间": max(response_times),
                "最小响应时间": min(response_times),
                "响应时间中位数": statistics.median(response_times),
                "响应时间标准差": statistics.stdev(response_times)
                if len(response_times) > 1
                else 0,
            }

    # 生成测试报告
    report = {
        "测试时间": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "总体统计": {
            "总请求数": len(results),
            "成功请求数": len(successful_requests),
            "失败请求数": len(failed_requests),
            "总体成功率": (len(successful_requests) / len(results)) * 100,
            "实际测试时长": time.time() - start_time,
            "平均RPS": len(results) / (time.time() - start_time),
        },
        "按路径统计": path_stats,
        "错误信息": [{"path": r["path"], "error": r["error"]} for r in failed_requests],
    }

    # 保存详细的JSON报告
    with open("loadtest_results.json", "w", encoding="utf-8") as f:
        json.dump(report, f, ensure_ascii=False, indent=2)

    # 保存人类可读的摘要报告
    with open("loadtest_results.txt", "w", encoding="utf-8") as f:
        f.write("压力测试报告\n")
        f.write(f"生成时间: {report['测试时间']}\n\n")

        f.write("=== 总体统计 ===\n")
        for key, value in report["总体统计"].items():
            f.write(f"{key}: {value:.2f}\n")

        f.write("\n=== 各路径统计 ===\n")
        for path, stats in report["按路径统计"].items():
            f.write(f"\n路径: {path}\n")
            for key, value in stats.items():
                f.write(f"{key}: {value:.2f}\n")

        if report["错误信息"]:
            f.write("\n=== 错误信息 ===\n")
            for error in report["错误信息"][:10]:  # 只显示前10个错误
                f.write(f"路径 {error['path']}: {error['error']}\n")


if __name__ == "__main__":
    # 可以通过命令行参数配置这些值
    run_load_test(
        total_requests=50000,  # 总请求数
        concurrent_users=100,  # 并发用户数
        duration=300,  # 最大运行时间（秒）
    )
