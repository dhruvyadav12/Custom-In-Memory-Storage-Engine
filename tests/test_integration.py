#!/usr/bin/env python3
"""
Integration Test Suite for Custom In-Memory Storage Engine
Runs end-to-end multi-client TCP network tests.
"""

import sys
import os
import time
import subprocess
import socket
import unittest

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "client")))
from client import StorageClient, TLVType

SERVER_PORT = 6389
HOST = "127.0.0.1"

class TestStorageEngineIntegration(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # We assume server is running or we can test if server is accessible
        cls.server_available = False
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(1.0)
            s.connect((HOST, SERVER_PORT))
            s.close()
            cls.server_available = True
        except Exception:
            # Try default port 6379 as fallback
            try:
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s.settimeout(1.0)
                s.connect((HOST, 6379))
                s.close()
                cls.server_available = True
                cls.port = 6379
            except Exception:
                cls.server_available = False

    def setUp(self):
        if not self.server_available:
            self.skipTest("Storage Engine server is not running on localhost. Skipping live socket integration test.")
        self.cli = StorageClient(host=HOST, port=getattr(self, 'port', SERVER_PORT), use_tlv=False)
        self.tlv_cli = StorageClient(host=HOST, port=getattr(self, 'port', SERVER_PORT), use_tlv=True)

    def tearDown(self):
        if self.server_available:
            self.cli.close()
            self.tlv_cli.close()

    def test_text_strings(self):
        res = self.cli.execute_command("SET", "t_key", "hello_text")
        self.assertIn("OK", res)
        val = self.cli.execute_command("GET", "t_key")
        self.assertIn("hello_text", val)

    def test_tlv_strings(self):
        res = self.tlv_cli.set("tlv_k", "tlv_v")
        self.assertEqual(res, "OK")
        val = self.tlv_cli.get("tlv_k")
        self.assertEqual(val, "tlv_v")

    def test_avl_sorted_set(self):
        self.tlv_cli.zadd("leaderboard", 100.0, "alice")
        self.tlv_cli.zadd("leaderboard", 300.0, "charlie")
        self.tlv_cli.zadd("leaderboard", 200.0, "bob")

        # Range query on AVL tree
        items = self.tlv_cli.zrangebyscore("leaderboard", 150.0, 350.0)
        self.assertIn("bob", items)
        self.assertIn("charlie", items)
        self.assertNotIn("alice", items)

    def test_ttl_expiration(self):
        self.tlv_cli.set("expire_me", "temp_value", px=150)
        self.assertEqual(self.tlv_cli.get("expire_me"), "temp_value")
        time.sleep(0.2)
        self.assertIsNone(self.tlv_cli.get("expire_me"))

if __name__ == "__main__":
    unittest.main()
