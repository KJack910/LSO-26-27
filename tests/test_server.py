import re
import socket
import subprocess
import sys
import time


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def request(port, payload):
    with socket.create_connection(("127.0.0.1", port), timeout=3) as sock:
        sock.sendall((payload + "\n").encode("utf-8"))
        data = bytearray()
        while not data.endswith(b"\n"):
            chunk = sock.recv(4096)
            if not chunk:
                raise AssertionError("server closed before replying")
            data.extend(chunk)
        return data.decode("utf-8").rstrip("\r\n")


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


def hello(port, name, token=None):
    payload = f"HELLO|{name}" if token is None else f"HELLO|{name}|{token}"
    fields = request(port, payload).split("|")
    assert fields[0] == "OK", fields
    assert len(fields) == 4, fields
    return fields[1], fields[3]


def setup_game(port):
    alice_id, alice_token = hello(port, "Alice")
    bob_id, bob_token = hello(port, "Bob")
    session = request(port, f"CREATE|{alice_id}|{alice_token}").split("|")[1]
    assert re.fullmatch(r"S[0-9]{5}", session)
    assert request(port, f"JOIN|{bob_id}|{session}|{bob_token}") == "OK|request sent"
    assert request(port, f"DECIDE|{alice_id}|{session}|1|{alice_token}") == "OK|accepted"
    fleet = (5, 4, 3, 3, 2)
    for player_id, token, row in ((alice_id, alice_token, 0), (bob_id, bob_token, 5)):
        for offset, length in enumerate(fleet):
            assert request(
                port,
                f"PLACE|{player_id}|{session}|{row + offset}|0|H|{length}|{token}",
            ) == "OK|placed"
        assert request(port, f"READY|{player_id}|{session}|{token}").startswith("OK|")
    return alice_id, alice_token, bob_id, bob_token, session


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

        # The same display name creates two independent identities.
        first_id, first_token = hello(port, "Duplicate")
        second_id, second_token = hello(port, "Duplicate")
        assert first_id != second_id
        assert request(port, f"CREATE|{first_id}|{second_token}").startswith("ERR|")
        session = request(port, f"CREATE|{first_id}|{first_token}").split("|")[1]
        assert request(port, f"QUIT|{first_id}|{first_token}") == "OK|quit allowed"
        assert request(port, f"HELLO|Duplicate|{first_token}").startswith("ERR|")

        # Full protocol and the generic target hit marker.
        alice_id, alice_token, bob_id, bob_token, session = setup_game(port)
        view = request(port, f"VIEW|{alice_id}|{session}|{alice_token}").split("|")
        assert view[:4] == ["OK", "PLAYING", "HOST", "Alice"]
        assert set(view[4]) == {"P", "C", "S", "I", "K", "."}
        assert set(view[5]) == {"."}
        assert request(port, f"SHOT|{alice_id}|{session}|5|0|{alice_token}") == "OK|HIT"
        view = request(port, f"VIEW|{alice_id}|{session}|{alice_token}").split("|")
        assert view[5][50] == "X", view[5]
        assert request(port, f"SURRENDER|{alice_id}|{session}|{alice_token}") == "OK|SURRENDER|WIN"
        finished = request(port, f"VIEW|{bob_id}|{session}|{bob_token}").split("|")
        assert finished[1] == "FINISHED" and finished[6] == "Bob"
        assert request(port, f"RESUME|{alice_id}|{alice_token}") == f"OK|{session}"
        assert request(port, f"REMATCH|{alice_id}|{session}|bad|{alice_token}").startswith("ERR|")

        # Pregame host loss promotes the connected guest and resets placement.
        host_id, host_token = hello(port, "PregameHost")
        guest_id, guest_token = hello(port, "PregameGuest")
        pregame = request(port, f"CREATE|{host_id}|{host_token}").split("|")[1]
        assert request(port, f"JOIN|{guest_id}|{pregame}|{guest_token}") == "OK|request sent"
        assert request(port, f"DECIDE|{host_id}|{pregame}|1|{host_token}") == "OK|accepted"
        deadline = time.monotonic() + 16
        while time.monotonic() < deadline:
            time.sleep(3)
            request(port, f"HEARTBEAT|{guest_id}|{pregame}|{guest_token}")
            state = request(port, f"VIEW|{guest_id}|{pregame}|{guest_token}").split("|")
            if state[2] == "HOST":
                break
        assert state[2] == "HOST", state
        assert state[1] in {"WAITING_REQUEST", "PLACEMENT", "WAITING_READY"}, state

        # Client can still start, authenticate, and exit through the UI.
        smoke_name = f"TerminalSmoke{port}"
        client_run = subprocess.run(
            [sys.argv[2], "127.0.0.1", str(port)],
            input=f"{smoke_name}\n0\n",
            capture_output=True,
            text=True,
            timeout=5,
            check=True,
        )
        assert f"Benvenuto, {smoke_name}" in client_run.stdout
        assert "Crea una partita" in client_run.stdout
        print("server protocol regression passed")
    finally:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)


if __name__ == "__main__":
    main()
