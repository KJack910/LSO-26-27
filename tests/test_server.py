import socket
import subprocess
import sys
import re
import time
from concurrent.futures import ThreadPoolExecutor


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def request(port, payload):
    with socket.create_connection(("127.0.0.1", port), timeout=2) as sock:
        sock.sendall((payload + "\n").encode("utf-8"))
        data = bytearray()
        while not data.endswith(b"\n"):
            chunk = sock.recv(2048)
            if not chunk:
                raise AssertionError("server closed before replying")
            data.extend(chunk)
        return data.decode("utf-8").rstrip("\r\n")


def send_incomplete_frame(port, payload):
    with socket.create_connection(("127.0.0.1", port), timeout=2) as sock:
        sock.sendall(payload.encode("utf-8"))
        sock.shutdown(socket.SHUT_WR)
        return sock.recv(2048)


def wait_for_server(port, process):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise AssertionError("server exited during startup")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                return
        except OSError:
            time.sleep(0.05)
    raise AssertionError("server did not start listening")


def play_to_win(port, host_id, guest_id, session_id, fleet):
    target_cells = [
        (row, col)
        for row, length in zip(range(5, 10), fleet)
        for col in range(length)
    ]
    guest_misses = [(row, 9) for row in range(9)] + [(row, 8) for row in range(8)]
    sunken_ship_ends = set()
    total = 0
    for length in fleet:
        total += length
        sunken_ship_ends.add(total - 1)

    for index, (row, col) in enumerate(target_cells):
        response = request(port, f"SHOT|{host_id}|{session_id}|{row}|{col}")
        if index == len(target_cells) - 1:
            expected = "OK|SUNK|WIN"
        elif index in sunken_ship_ends:
            expected = "OK|SUNK"
        else:
            expected = "OK|HIT"
        assert response == expected, response

        if index < len(target_cells) - 1:
            miss_row, miss_col = guest_misses[index]
            assert request(
                port,
                f"SHOT|{guest_id}|{session_id}|{miss_row}|{miss_col}",
            ) == "OK|MISS"


def main():
    port = free_port()
    process = subprocess.Popen(
        [sys.argv[1], "127.0.0.1", str(port)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        wait_for_server(port, process)
        assert send_incomplete_frame(port, "HELLO|NoNewline") == b""
        idle_clients = [socket.create_connection(("127.0.0.1", port), timeout=2) for _ in range(64)]
        overflow_client = socket.create_connection(("127.0.0.1", port), timeout=2)
        overflow_client.settimeout(2)
        assert overflow_client.recv(1) == b""
        overflow_client.close()
        for idle_client in idle_clients:
            idle_client.close()

        client_run = subprocess.run(
            [sys.argv[2], "127.0.0.1", str(port)],
            input="TerminalSmoke\n0\n",
            capture_output=True,
            text=True,
            timeout=5,
            check=True,
        )
        assert "Benvenuto, TerminalSmoke" in client_run.stdout
        assert "Crea una partita" in client_run.stdout

        alice = request(port, "HELLO|Alice").split("|")
        bob = request(port, "HELLO|Bob").split("|")
        assert alice[0] == bob[0] == "OK"
        assert alice[1] != bob[1]
        assert request(port, "HELLO|Alice").split("|")[1] == alice[1]

        with ThreadPoolExecutor(max_workers=8) as pool:
            concurrent = list(pool.map(
                lambda index: request(port, f"HELLO|Concurrent-{index}").split("|"),
                range(24),
            ))
        concurrent_ids = [entry[1] for entry in concurrent]
        assert len(set(concurrent_ids)) == len(concurrent_ids)

        session = request(port, f"CREATE|{alice[1]}").split("|")
        assert session[0] == "OK", session
        session_id = session[1]
        assert re.fullmatch(r"S[0-9]{5}", session_id)
        assert request(port, f"CREATE|{alice[1]}").startswith("ERR|")
        other_host = request(port, "HELLO|OtherHost").split("|")[1]
        second_session = request(port, f"CREATE|{other_host}").split("|")[1]
        assert second_session != session_id
        assert request(port, "LIST").startswith("OK|" + session_id)
        client_game = subprocess.run(
            [sys.argv[2], "127.0.0.1", str(port)],
            input="TerminalHost\n1\n",
            capture_output=True,
            text=True,
            timeout=5,
            check=True,
        )
        assert "Sessione creata: S" in client_game.stdout
        assert "Accesso diretto alla lobby" in client_game.stdout
        assert "  4  Accetta" not in client_game.stdout
        assert "  5  Apri" not in client_game.stdout
        waiting = request(port, f"VIEW|{alice[1]}|{session_id}").split("|")
        assert waiting[:4] == ["OK", "WAITING_REQUEST", "HOST", "-"]
        grid_client = subprocess.run(
            [sys.argv[2], "127.0.0.1", str(port)],
            input=f"GridSmoke\n3\n{session_id}\n",
            capture_output=True,
            text=True,
            timeout=5,
            check=True,
        )
        assert "LA TUA FLOTTA" in grid_client.stdout
        assert any(
            "LA TUA FLOTTA" in line and "BERSAGLI SULLA GRIGLIA AVVERSARIA" in line
            for line in grid_client.stdout.splitlines()
        )
        assert request(port, f"DECIDE|{alice[1]}|{session_id}|0") == "OK|rejected"
        assert request(port, f"JOIN|{bob[1]}|{session_id}").startswith("OK|")
        pending = request(port, f"VIEW|{bob[1]}|{session_id}").split("|")
        assert pending[:4] == ["OK", "WAITING_ACCEPT", "GUEST", "-"]
        assert request(port, f"CREATE|{bob[1]}").startswith("ERR|")
        assert request(port, f"JOIN|{bob[1]}|{second_session}").startswith("ERR|")
        assert request(port, f"DECIDE|{alice[1]}|{session_id}|1").startswith("OK|")
        assert request(port, f"PLACE|{alice[1]}|{session_id}|garbage|0|H|5").startswith("ERR|")
        assert request(port, f"LEAVE|{alice[1]}|{session_id}").startswith("ERR|")
        assert request(port, f"QUIT|{alice[1]}").startswith("ERR|")
        view = request(port, f"VIEW|{alice[1]}|{session_id}").split("|")
        assert len(view) == 6 and view[:4] == ["OK", "PLACEMENT", "HOST", "-"]
        assert len(view[4]) == len(view[5]) == 100
        assert set(view[4]) == set(view[5]) == {"."}

        fleet = (5, 4, 3, 3, 2)
        for player_id, row in ((alice[1], 0), (bob[1], 5)):
            for offset, length in enumerate(fleet):
                result = request(
                    port,
                    f"PLACE|{player_id}|{session_id}|{row + offset}|0|H|{length}",
                )
                assert result == "OK|placed", result
            ready = request(port, f"READY|{player_id}|{session_id}")
        assert ready == "OK|game started", ready
        view = request(port, f"VIEW|{alice[1]}|{session_id}").split("|")
        assert view[:4] == ["OK", "PLAYING", "HOST", "Alice"]
        assert set(view[4]) == {"S", "."}
        assert set(view[5]) == {"."}

        assert request(port, f"SHOT|{alice[1]}|{session_id}|garbage|0").startswith("ERR|")
        assert request(port, f"SHOT|{alice[1]}|{session_id}|9|9") == "OK|MISS"
        assert request(port, f"SHOT|{alice[1]}|{session_id}|9|8").startswith("ERR|")
        assert request(port, f"SHOT|{bob[1]}|{session_id}|9|9") == "OK|MISS"
        assert request(port, f"SHOT|{bob[1]}|{session_id}|9|9").startswith("ERR|")
        view = request(port, f"VIEW|{alice[1]}|{session_id}").split("|")
        assert view[4][99] == "o" and view[5][99] == "o"

        play_to_win(port, alice[1], bob[1], session_id, fleet)
        view = request(port, f"VIEW|{alice[1]}|{session_id}").split("|")
        assert view[:4] == ["OK", "FINISHED", "HOST", "-"]
        assert view[5][50] == "X"
        assert request(port, f"QUIT|{alice[1]}") == "OK|quit allowed"
        assert request(port, f"QUIT|{bob[1]}") == "OK|quit allowed"

        assert request(port, f"REMATCH|{alice[1]}|{session_id}|same") == "OK|rematch started"
        assert request(port, f"STATUS|{alice[1]}|{session_id}").endswith("|0|0")
        for player_id, row in ((alice[1], 0), (bob[1], 5)):
            for offset, length in enumerate(fleet):
                assert request(
                    port,
                    f"PLACE|{player_id}|{session_id}|{row + offset}|0|H|{length}",
                ) == "OK|placed"
            request(port, f"READY|{player_id}|{session_id}")
        assert request(port, f"SURRENDER|{alice[1]}|{session_id}") == "OK|SURRENDER|WIN"
        assert request(port, f"VIEW|{bob[1]}|{session_id}").split("|")[1] == "FINISHED"

        assert request(port, f"REMATCH|{alice[1]}|{session_id}|same") == "OK|rematch started"
        for player_id, row in ((alice[1], 0), (bob[1], 5)):
            for offset, length in enumerate(fleet):
                assert request(
                    port,
                    f"PLACE|{player_id}|{session_id}|{row + offset}|0|H|{length}",
                ) == "OK|placed"
            request(port, f"READY|{player_id}|{session_id}")
        play_to_win(port, alice[1], bob[1], session_id, fleet)
        view = request(port, f"VIEW|{alice[1]}|{session_id}").split("|")
        assert view[:4] == ["OK", "FINISHED", "HOST", "-"]
        assert view[5][50] == "X"
        assert request(port, f"REMATCH|{alice[1]}|{session_id}|new") == "OK|session open for new player"
        assert session_id in request(port, "LIST")
        assert request(port, f"REMATCH|{bob[1]}|{session_id}|same").startswith("ERR|")
        process.terminate()
        process.wait(timeout=3)
        process = subprocess.Popen(
            [sys.argv[1], "127.0.0.1", str(port)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
        )
        wait_for_server(port, process)
        restarted_bob = request(port, "HELLO|Bob").split("|")[1]
        restarted_alice = request(port, "HELLO|Alice").split("|")[1]
        assert restarted_bob == "1" and restarted_alice == "2"
        print("server protocol integration passed")
    finally:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)


if __name__ == "__main__":
    main()
