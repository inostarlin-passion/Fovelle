"""S1: pause retains decoder; destructive transitions invalidate pending work."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class MovieResumeContracts(unittest.TestCase):
    def test_pause_lifecycle(self):
        source = (ROOT / 'src/qvmovie.cpp').read_text()
        pause = source.split('void QVMovie::setPaused(bool paused)')[1].split('void QVMovie::setSpeed')[0]
        self.assertNotIn('cancelNextLoad', pause)
        self.assertIn('nextImageTimer->stop()', pause)
        self.assertIn('nextLoadTime.reset()', pause)
        cancel = source.split('void QVMoviePrivate::cancelNextLoad()')[1].split('bool QVMoviePrivate::isDone')[0]
        for statement in ('++playbackGeneration', 'QvAsync::cancel(frameTicket)', 'asyncDecoder.reset()', 'pendingFrame.reset()'):
            self.assertIn(statement, cancel)
        load = source.split('void QVMoviePrivate::loadAsyncFrame()')[1].split('void QVMoviePrivate::_q_loadNextFrame(bool starting)')[0]
        self.assertIn('if (frameTicket)', load)
        self.assertIn('movieState == QVMovie::Paused', load)
        self.assertIn('pendingFrame = std::move(info)', load)
        self.assertIn('generation != playbackGeneration', load)
        cmake = (ROOT / 'tests/CMakeLists.txt').read_text()
        self.assertIn('FovelleMovieResumeStatic', cmake)
        self.assertIn('FovelleMovieResumeDynamic', cmake)

if __name__ == '__main__':
    unittest.main()
