import unittest
import requests
import time


class TestWebServer(unittest.TestCase):
    BASE_URL = "http://localhost:3490"

    def setUp(self):
        # 确保服务器运行
        self.session = requests.Session()

    def test_get_index(self):
        """测试访问根路径"""
        response = self.session.get(f"{self.BASE_URL}/")
        self.assertEqual(response.status_code, 200)
        self.assertIn("text/html", response.headers["Content-Type"])

    def test_get_d20(self):
        """测试 /d20 端点"""
        response = self.session.get(f"{self.BASE_URL}/d20")
        self.assertEqual(response.status_code, 200)
        self.assertIn("text/plain", response.headers["Content-Type"])
        # 检查返回值在1-20之间
        value = int(response.text)
        self.assertTrue(1 <= value <= 20)

    def test_404(self):
        """测试不存在的页面"""
        response = self.session.get(f"{self.BASE_URL}/not_exist")
        self.assertEqual(response.status_code, 404)

    def test_post_save(self):
        """测试POST保存内容"""
        test_content = "Hello Test Content"
        headers = {"Content-Type": "text/plain"}

        post_response = self.session.post(
            f"{self.BASE_URL}/save", data=test_content, headers=headers
        )
        self.assertEqual(post_response.status_code, 200)
        self.assertEqual(post_response.json()["status"], "ok")

        # 验证文件内容
        get_response = self.session.get(f"{self.BASE_URL}/save")
        self.assertEqual(get_response.text, test_content)

    def test_cache(self):
        """测试缓存功能，非常简单，详细的缓存测试在源代码部分"""
        # 首次请求
        t1 = time.time()
        self.session.get(f"{self.BASE_URL}/cat.jpg")
        t2 = time.time()

        # 第二次请求(应该走缓存)
        t3 = time.time()
        self.session.get(f"{self.BASE_URL}/cat.jpg")
        t4 = time.time()

        # 缓存的响应应该更快
        self.assertTrue((t4 - t3) < (t2 - t1))

    def test_bad_request(self):
        """测试错误请求"""
        # 发送错误的HTTP方法
        response = self.session.put(f"{self.BASE_URL}/")
        self.assertEqual(response.status_code, 404)


if __name__ == "__main__":
    unittest.main()
