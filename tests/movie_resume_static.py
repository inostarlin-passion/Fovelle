"""S1: pause retains decoder; destructive transitions invalidate pending work."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class MovieResumeContracts(unittest.TestCase):
    def test_resume_timer_observation(self):
        source = (ROOT / 'tests/remediation_tests.h').read_text()
        case = source.split('void testMoviePauseResume()')[1].split('void testMoviePauseQueuedRead_data()')[0]
        self.assertNotIn('timer->remainingTime() > 0', case)
        self.assertNotIn('movie.nextFrameDelay() - 50', case)
        self.assertIn('MovieTimerObservation observation', case)
        self.assertIn('observation.deliveries', case)
        self.assertIn('observation.valid', case)
        self.assertIn('frames.count(), count + 1', case)
        observer = source.split('class MovieTimerObservation')[1].split('class RemediationTests')[0]
        for check in ('QEvent::Timer', 'timer->isActive()', 'remaining >= 0',
                      'remaining <= interval', 'interval <= movie.nextFrameDelay()'):
            self.assertIn(check, observer)

    def test_delayed_resume_is_registered(self):
        cmake = (ROOT / 'tests/CMakeLists.txt').read_text()
        command = cmake.split('add_test(NAME FovelleMovieResumeDynamic')[1].split('set_tests_properties')[0]
        self.assertIn('testMovieResumeDelayedDispatch', command)
        source = (ROOT / 'tests/remediation_tests.h').read_text()
        for check in ('"async-queue"', '"gui-dispatch"', 'release->release(4)',
                      'QThread::msleep', 'QCOMPARE(observation.remaining, 0)'):
            self.assertIn(check, source)

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
