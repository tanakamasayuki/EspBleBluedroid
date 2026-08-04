import re


def start(espble, bluedroid, mode):
    """Bring both boards up in one security mode and clear the NVS bonds.

    The mode is chosen at runtime rather than at build time so one firmware pair
    serves both tests. Bonds are cleared after `begin()` because they live in NVS:
    a bond left by an earlier run would let this one re-encrypt from the stored
    keys and pass without ever pairing.
    """
    espble.write("?")
    espble.expect(re.compile(rb"ESPBLE_SECURITY_STATE started=\d"), timeout=40)
    bluedroid.write("?")
    bluedroid.expect(re.compile(rb"READY_STATE started=\d"), timeout=40)

    # `M` is `m` with this side answering no to the comparison, so the peer runs
    # the same configuration either way: only the library under test refuses.
    #
    # Both libraries spell the EspBleError names the same way, so the same string
    # is asserted on both boards. That agreement is machine-checked by
    # `unit/api_parity`; here it simply reads naturally.
    peer_mode = "m" if mode == "M" else mode
    espble.write(peer_mode)
    espble.expect_exact("ESPBLE_MODE_STARTED mode=%s ok=1 error=NONE" % peer_mode,
                        timeout=30)
    bluedroid.write(mode)
    bluedroid.expect_exact("MODE_STARTED mode=%s ok=1 error=NONE" % mode,
                           timeout=30)

    espble.write("x")
    espble.expect_exact("ESPBLE_BONDS_CLEARED success=1 count=0", timeout=20)
    bluedroid.write("x")
    bluedroid.expect_exact("BONDS_CLEARED success=1 count=0", timeout=20)


def connect(espble, bluedroid):
    bluedroid.write("c")
    bluedroid.expect_exact("SCAN_STARTED 1", timeout=10)
    # The peer is selected by its 128-bit service UUID in firmware; the name is
    # captured only as information, because it travels in the scan response and a
    # missing scan response is not what this scenario is about.
    bluedroid.expect(
        re.compile(rb"TARGET_FOUND address=([0-9a-f:]+) name=(.*)"), timeout=40
    )
    bluedroid.expect_exact("CONNECT_REQUESTED 1", timeout=10)
    bluedroid.expect(
        re.compile(rb"CONNECTED id=\d+ encrypted=0 bonded=0 context=loop"),
        timeout=25,
    )
    espble.expect(re.compile(rb"ESPBLE_CONNECTED id=\d+"), timeout=20)


def test_just_works_bonding_across_stacks(dut, peers):
    """Just Works pairing and bonding between the two host stacks.

    SMP is negotiated between two independent implementations here, so the
    assertion is what *both* sides report about the same link — encrypted,
    authenticated, bonded and the key size — rather than one side's opinion.
    `peer/security_bond` pins the same numbers with Bluedroid on both ends, where
    a shared assumption about the association model cancels itself out.

    The attribute tiers are exercised in the same session: an encrypted
    characteristic must be reachable, and an authenticated one must be refused,
    because Just Works produces an unauthenticated link no matter which stack
    negotiated it.
    """
    espble = dut
    bluedroid = peers["device"]
    start(espble, bluedroid, "j")
    connect(espble, bluedroid)

    # No IO capability on either side, so the association model is Just Works:
    # encrypted and bonded, but not authenticated.
    bluedroid.expect_exact(
        "SECURITY success=1 encrypted=1 authenticated=0 bonded=1 key=16 "
        "context=loop",
        timeout=40,
    )
    espble.expect_exact(
        "ESPBLE_SECURITY success=1 encrypted=1 authenticated=0 bonded=1 key=16",
        timeout=30,
    )

    # Both sides recorded the bond, which is what makes it a bond rather than a
    # one-off encrypted session.
    bluedroid.write("b")
    bluedroid.expect_exact("BONDS count=1", timeout=20)
    espble.write("b")
    espble.expect_exact("ESPBLE_BONDS count=1", timeout=20)

    bluedroid.write("d")
    bluedroid.expect_exact("DISCOVERY_REQUESTED 1", timeout=10)
    discovery = bluedroid.expect(
        re.compile(rb"DISCOVERY success=1 services=(\d+) context=loop"),
        timeout=30,
    )
    assert int(discovery.group(1)) >= 1

    # The encrypted tier is reachable on an encrypted link, in both directions.
    bluedroid.write("e")
    bluedroid.expect_exact("ENCRYPTED_READ_REQUESTED 1", timeout=10)
    bluedroid.expect_exact(
        "READ success=1 error=NONE value=encrypted-ready context=loop",
        timeout=25,
    )
    bluedroid.write("E")
    bluedroid.expect_exact("ENCRYPTED_WRITE_REQUESTED 1", timeout=10)
    bluedroid.expect_exact("WRITE success=1 error=NONE context=loop", timeout=25)
    espble.expect_exact(
        "ESPBLE_WRITE tier=encrypted value=central-encrypted-write", timeout=20
    )

    # The authenticated tier is not: the peer has to refuse it, and this side has
    # to surface the refusal as a failed completion instead of an empty success.
    bluedroid.write("a")
    bluedroid.expect_exact("AUTHENTICATED_READ_REQUESTED 1", timeout=10)
    refused = bluedroid.expect(
        re.compile(rb"READ success=(\d) error=(\w+) value=(\S*) context=loop"),
        timeout=25,
    )
    assert refused.group(1) == b"0", (
        "an unauthenticated Just Works link must not reach an authenticated "
        "characteristic; the peer returned %s" % refused.group(3).decode()
    )

    bluedroid.write("X")
    bluedroid.expect_exact("DISCONNECT_REQUESTED 1", timeout=10)
    bluedroid.expect(
        re.compile(rb"DISCONNECTED id=\d+ encrypted=1 bonded=1 context=loop"),
        timeout=25,
    )
    espble.expect(re.compile(rb"ESPBLE_DISCONNECTED id=\d+"), timeout=20)


def test_static_passkey_authenticates_across_stacks(dut, peers):
    """Passkey Entry between the two stacks, and the tier it unlocks.

    The central is DisplayOnly and the peripheral KeyboardOnly, which is what
    selects Passkey Entry: two DisplayOnly sides would fall back to Just Works and
    stay unauthenticated however the passkey is configured. The passkey is static
    and shared with `peer/security_passkey`, since that is the only passkey model a
    test can drive unattended.

    What this adds over the peer scenario is that the association model is chosen
    by two different implementations from the exchanged IO capabilities, and the
    authenticated attribute tier is then reachable on the resulting link.
    """
    espble = dut
    bluedroid = peers["device"]
    start(espble, bluedroid, "p")
    connect(espble, bluedroid)

    # This side displays the passkey; the peer enters the same static value.
    bluedroid.expect_exact("PASSKEY_DISPLAYED passkey=438209 context=loop",
                           timeout=40)
    bluedroid.expect_exact(
        "SECURITY success=1 encrypted=1 authenticated=1 bonded=1 key=16 "
        "context=loop",
        timeout=40,
    )
    espble.expect_exact(
        "ESPBLE_SECURITY success=1 encrypted=1 authenticated=1 bonded=1 key=16",
        timeout=30,
    )

    bluedroid.write("d")
    bluedroid.expect_exact("DISCOVERY_REQUESTED 1", timeout=10)
    bluedroid.expect(
        re.compile(rb"DISCOVERY success=1 services=\d+ context=loop"), timeout=30
    )

    # Both tiers are reachable now, and the peer reports which one each write
    # landed on.
    bluedroid.write("e")
    bluedroid.expect_exact("ENCRYPTED_READ_REQUESTED 1", timeout=10)
    bluedroid.expect_exact(
        "READ success=1 error=NONE value=encrypted-ready context=loop",
        timeout=25,
    )
    bluedroid.write("a")
    bluedroid.expect_exact("AUTHENTICATED_READ_REQUESTED 1", timeout=10)
    bluedroid.expect_exact(
        "READ success=1 error=NONE value=authenticated-ready context=loop",
        timeout=25,
    )
    bluedroid.write("A")
    bluedroid.expect_exact("AUTHENTICATED_WRITE_REQUESTED 1", timeout=10)
    bluedroid.expect_exact("WRITE success=1 error=NONE context=loop", timeout=25)
    espble.expect_exact(
        "ESPBLE_WRITE tier=authenticated value=central-authenticated-write",
        timeout=20,
    )

    bluedroid.write("X")
    bluedroid.expect_exact("DISCONNECT_REQUESTED 1", timeout=10)
    bluedroid.expect(
        re.compile(rb"DISCONNECTED id=\d+ encrypted=1 bonded=1 context=loop"),
        timeout=25,
    )


def test_numeric_comparison_confirmed_across_stacks(dut, peers):
    """Numeric Comparison: the same six digits derived by two implementations.

    DisplayYesNo on both sides and no passkey, so LE Secure Connections selects
    Numeric Comparison. The number is derived from the exchanged public keys, and
    each host computes it independently — so the assertion is that the two numbers
    are **equal**, which is the whole safety property of the model. A cross-stack
    mismatch would mean a user comparing two screens is told to accept something
    the stacks do not agree on.

    `peer/numeric_comparison` covers confirm / reject / timeout with Bluedroid on
    both ends, where one derivation would be compared against itself.
    """
    espble = dut
    bluedroid = peers["device"]
    start(espble, bluedroid, "m")
    connect(espble, bluedroid)

    ours = bluedroid.expect(
        re.compile(rb"NUMERIC number=(\d{6}) accept=1 answered=1 context=loop"),
        timeout=40,
    )
    theirs = espble.expect(
        re.compile(rb"ESPBLE_NUMERIC number=(\d{6}) confirmed=1"), timeout=30
    )
    assert ours.group(1) == theirs.group(1), (
        "the two stacks derived different numbers (%s here, %s on the peer); a "
        "user comparing two screens would be asked to accept a mismatch"
        % (ours.group(1).decode(), theirs.group(1).decode())
    )

    # Confirming on both sides yields an authenticated, bonded link — the same
    # tier Passkey Entry reaches, by a different association model.
    bluedroid.expect_exact(
        "SECURITY success=1 encrypted=1 authenticated=1 bonded=1 key=16 "
        "context=loop",
        timeout=40,
    )
    espble.expect_exact(
        "ESPBLE_SECURITY success=1 encrypted=1 authenticated=1 bonded=1 key=16",
        timeout=30,
    )
    bluedroid.write("d")
    bluedroid.expect_exact("DISCOVERY_REQUESTED 1", timeout=10)
    bluedroid.expect(
        re.compile(rb"DISCOVERY success=1 services=\d+ context=loop"), timeout=30
    )
    bluedroid.write("a")
    bluedroid.expect_exact("AUTHENTICATED_READ_REQUESTED 1", timeout=10)
    bluedroid.expect_exact(
        "READ success=1 error=NONE value=authenticated-ready context=loop",
        timeout=25,
    )

    bluedroid.write("X")
    bluedroid.expect_exact("DISCONNECT_REQUESTED 1", timeout=10)
    bluedroid.expect(
        re.compile(rb"DISCONNECTED id=\d+ encrypted=1 bonded=1 context=loop"),
        timeout=25,
    )


def test_numeric_comparison_rejected_leaves_nothing_behind(dut, peers):
    """Answering no has to fail the pairing and leave no bond on either side.

    The peer accepts and this side refuses, so the refusal is what has to travel:
    a stack that treated the local "no" as a local matter would still end up
    encrypted with a peer that thinks the user confirmed. The link is left
    unencrypted and unbonded, and the encrypted characteristic stays out of reach.
    """
    espble = dut
    bluedroid = peers["device"]
    start(espble, bluedroid, "M")
    connect(espble, bluedroid)

    bluedroid.expect(
        re.compile(rb"NUMERIC number=(\d{6}) accept=0 answered=1 context=loop"),
        timeout=40,
    )
    security = bluedroid.expect(
        re.compile(
            rb"SECURITY success=(\d) encrypted=(\d) authenticated=\d bonded=(\d) "
            rb"key=\d+ context=loop"
        ),
        timeout=40,
    )
    assert security.group(1) == b"0", "a refused comparison must not succeed"
    assert security.group(2) == b"0", "a refused comparison must not encrypt"
    assert security.group(3) == b"0", "a refused comparison must not bond"

    bluedroid.write("b")
    bluedroid.expect_exact("BONDS count=0", timeout=20)
    espble.write("b")
    espble.expect_exact("ESPBLE_BONDS count=0", timeout=20)
