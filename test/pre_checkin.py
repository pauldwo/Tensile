"""
These pre-checkin tests trade-off time and coverage, favoring time.
"""

import Tensile.Tensile as Tensile

# defaults
def test_hgemm_defaults(tmpdir):
    Tensile.Tensile([Tensile.TensileConfigPath("test_hgemm_defaults.yaml"), tmpdir.strpath])
def test_sgemm_defaults(tmpdir):
    Tensile.Tensile([Tensile.TensileConfigPath("test_sgemm_defaults.yaml"), tmpdir.strpath])
def test_dgemm_defaults(tmpdir):
    Tensile.Tensile([Tensile.TensileConfigPath("test_dgemm_defaults.yaml"), tmpdir.strpath])

