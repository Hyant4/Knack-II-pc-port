"""Production demuxer regression with generated video/audio, no game or GPU."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
try:
    from tests.native_toolchain import c_compiler
except ModuleNotFoundError:
    from native_toolchain import c_compiler

ROOT=Path(__file__).resolve().parents[1]


class AvPlayerLoop(unittest.TestCase):
    def test_repeated_demux_seek_failure_empty_restart_and_non_loop_eof(self):
        cc=c_compiler()
        cxx=cc.with_name('clang++.exe') if os.name=='nt' else Path(shutil.which('c++') or '')
        ffmpeg=cc.parent/'ffmpeg.exe' if os.name=='nt' else Path(shutil.which('ffmpeg') or '')
        if not cxx.is_file() or not ffmpeg.is_file():
            self.skipTest('Installed C++ compiler and FFmpeg executable are required')
        env={k:v for k,v in os.environ.items() if not k.startswith(('TLG_','SDL_'))}
        env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
        if os.name=='nt':
            flags=['-I'+str(cc.parent.parent/'include'),'-L'+str(cc.parent.parent/'lib'),
                   '-lavformat','-lavcodec','-lavutil']
        else:
            flags=subprocess.check_output(['pkg-config','--cflags','--libs',
                                           'libavformat','libavcodec','libavutil'],text=True).split()
        source=(ROOT/'port/gpu/shadps4/core/libraries/avplayer/avplayer_source.cpp').read_text(encoding='utf-8')
        start=source.index('void AvPlayerSource::DemuxerThread(')
        end=source.index('\nAvPlayerSource::AVFramePtr',start)
        with tempfile.TemporaryDirectory(prefix='knack-avplayer-loop-') as folder:
            folder=Path(folder)
            (folder/'avplayer_demuxer.inc').write_text(source[start:end],encoding='utf-8')
            exe=folder/'demux.exe'
            build=subprocess.run([str(cxx),'-std=c++20','-O2','-Wall','-Wextra','-Werror',
                '-I'+str(folder),str(ROOT/'tests/test_avplayer_loop.cpp'),*flags,'-o',str(exe)],
                env=env,capture_output=True,text=True,timeout=60)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            summaries=[]
            for kind in ('video','audio','both'):
                media=folder/(kind+'.mp4');command=[str(ffmpeg),'-v','error','-y']
                if kind!='audio':command+=['-f','lavfi','-i','testsrc2=size=128x128:rate=30']
                if kind!='video':command+=['-f','lavfi','-i','sine=frequency=440:sample_rate=48000']
                command+=['-t','1']
                if kind!='audio':command+=['-c:v','libx264','-preset','ultrafast','-crf','0','-pix_fmt','yuv420p']
                if kind!='video':command+=['-c:a','aac']
                command+=[str(media)]
                generated=subprocess.run(command,env=env,capture_output=True,text=True,timeout=20)
                self.assertEqual(generated.returncode,0,generated.stderr)
                for custom_io in (False,True):
                    for mode in (0,1,2,3):
                        with self.subTest(media=kind,custom_io=custom_io,mode=mode):
                            args=[str(exe),str(media),str(mode)]+(['--custom-io'] if custom_io else [])
                            run=subprocess.run(args,env=env,capture_output=True,text=True,timeout=10)
                            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
                            self.assertIn('PASS ',run.stdout)
                            summaries.append(kind+' custom_io='+str(custom_io)+' '+str(mode)+' '+run.stdout.strip())
            if os.environ.get('KNACK_AVPLAYER_TEST_LOG'):
                Path(os.environ['KNACK_AVPLAYER_TEST_LOG']).write_text('\n'.join(summaries),encoding='utf-8')


if __name__=='__main__':
    unittest.main()
