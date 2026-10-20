import pytest
from pytest_embedded import Dut


@pytest.mark.parametrize('target', ['linux'], indirect=True)
def test_host_logic(dut: Dut) -> None:
    dut.run_all_single_board_cases(timeout=60)
