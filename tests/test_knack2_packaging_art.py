"""Packaging must reject missing images instead of silently shipping an empty hero."""
import os
from pathlib import Path
import tempfile,unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
SPEC=ROOT/'port/launcher/knack2/packaging/knack2.spec'

class PackagingArtwork(unittest.TestCase):
    def test_missing_artwork_is_rejected_and_complete_artwork_is_collected(self):
        prefix=SPEC.read_text(encoding='utf-8').split('unused =',1)[0]
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);ui=root/'port/launcher/knack2/ui';ui.mkdir(parents=True)
            with patch.dict(os.environ,KNACK_BUILD_ROOT=str(root)):
                with self.assertRaisesRegex(ValueError,'Required launcher artwork missing'):
                    exec(compile(prefix,str(SPEC),'exec'),{})
                art=ui/'art';art.mkdir()
                names=['knack-keyart.jpg','knack-gameplay.png','controller-ps-photo.png',
                       'controller-xbox-one-frontal.png','controller-nintendo-front-clean.png']
                for name in names:(art/name).write_bytes(b'synthetic resource')
                namespace={};exec(compile(prefix,str(SPEC),'exec'),namespace)
                collected={Path(source).name for source,destination in namespace['datas']}
                self.assertTrue(set(names)<=collected)
if __name__=='__main__':unittest.main()
