import og.__main__ as cli


def _record(monkeypatch, failing=()):
    calls = []
    real = cli.main

    def fake(argv=None):
        if argv and argv[0] != "check":
            calls.append(list(argv))
            return 1 if argv[0] in failing else 0
        return real(argv)

    monkeypatch.setattr(cli, "main", fake)
    return calls


def test_check_runs_every_check_in_order(monkeypatch):
    calls = _record(monkeypatch)
    assert cli.main(["check", "--staged"]) == 0
    assert calls == [["secrets", "--staged"], ["assets", "check"],
                     ["decisions", "check", "--offline"]]


def test_check_reports_failure_but_still_runs_the_rest(monkeypatch, capsys):
    calls = _record(monkeypatch, failing={"secrets"})
    assert cli.main(["check", "--tracked", "--lfs-pointers-ok", "notes.txt"]) == 1
    assert calls[0] == ["secrets", "notes.txt", "--tracked", "--lfs-pointers-ok"]
    assert len(calls) == 3
    assert "failed: secrets" in capsys.readouterr().err


def test_check_only_selects_checks(monkeypatch):
    calls = _record(monkeypatch)
    assert cli.main(["check", "--only", "assets", "--only", "decisions"]) == 0
    assert [c[0] for c in calls] == ["assets", "decisions"]


def test_check_needs_a_secrets_scope(monkeypatch):
    calls = _record(monkeypatch)
    assert cli.main(["check"]) == 2
    assert calls == []
