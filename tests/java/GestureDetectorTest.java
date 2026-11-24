import android.os.Handler;
import android.os.Looper;
import android.view.GestureDetector;
import android.view.MotionEvent;

/** android.view.GestureDetector: taps, scrolls, flings, double taps, long presses and cancels. */
public final class GestureDetectorTest {
  static int failures;

  static void check(boolean ok, String what) {
    if (!ok) {
      failures++;
      System.out.println("FAIL: " + what);
    }
  }

  /** Counts what the detector reports. */
  static final class Recorder extends GestureDetector.SimpleOnGestureListener {
    int down, showPress, singleTapUp, scroll, longPress, fling, doubleTap, doubleTapEvent, confirmed;
    float lastDistanceX, lastDistanceY, lastVelocityX;
    MotionEvent scrollFirst;
    @Override public boolean onDown(MotionEvent e) { down++; return true; }
    @Override public void onShowPress(MotionEvent e) { showPress++; }
    @Override public boolean onSingleTapUp(MotionEvent e) { singleTapUp++; return true; }
    @Override public boolean onScroll(MotionEvent e1, MotionEvent e2, float dx, float dy) {
      scroll++; lastDistanceX = dx; lastDistanceY = dy; scrollFirst = e1; return true;
    }
    @Override public void onLongPress(MotionEvent e) { longPress++; }
    @Override public boolean onFling(MotionEvent e1, MotionEvent e2, float vx, float vy) {
      fling++; lastVelocityX = vx; return true;
    }
    @Override public boolean onDoubleTap(MotionEvent e) { doubleTap++; return true; }
    @Override public boolean onDoubleTapEvent(MotionEvent e) { doubleTapEvent++; return true; }
    @Override public boolean onSingleTapConfirmed(MotionEvent e) { confirmed++; return true; }
  }

  static MotionEvent event(long down, long time, int action, float x, float y) {
    return MotionEvent.obtain(down, time, action, x, y, 0);
  }

  /** Runs body on a thread with its own Looper, which quits after waitMillis, so timers can fire. */
  static void withLooper(long waitMillis, java.util.function.Consumer<Looper> body) throws Exception {
    Thread thread = new Thread(() -> {
      Looper.prepare();
      body.accept(Looper.myLooper());
      new Handler(Looper.myLooper()).postDelayed(() -> Looper.myLooper().quit(), waitMillis);
      Looper.loop();
    });
    thread.start();
    thread.join();
  }

  public static void main(String[] args) throws Exception {
    Looper.prepare();

    // A quick touch and release in place is a tap.
    Recorder r = new Recorder();
    GestureDetector d = new GestureDetector(null, r);
    d.onTouchEvent(event(0, 0, MotionEvent.ACTION_DOWN, 100, 100));
    d.onTouchEvent(event(0, 50, MotionEvent.ACTION_UP, 100, 100));
    check(r.down == 1 && r.singleTapUp == 1 && r.scroll == 0 && r.fling == 0, "tap: down+singleTapUp only");

    // Moving past the touch slop scrolls; the distance is the previous position minus the new one.
    r = new Recorder();
    d = new GestureDetector(null, r);
    MotionEvent down = event(0, 0, MotionEvent.ACTION_DOWN, 100, 100);
    d.onTouchEvent(down);
    d.onTouchEvent(event(0, 1000, MotionEvent.ACTION_MOVE, 100, 130));
    check(r.scroll == 1 && r.lastDistanceY == -30f && r.lastDistanceX == 0f, "scroll: reports distance, once past the slop");
    d.onTouchEvent(event(0, 2000, MotionEvent.ACTION_UP, 100, 130));
    check(r.fling == 0 && r.singleTapUp == 0, "slow release after a scroll is not a fling or a tap");

    // Moving less than the slop is still a tap.
    r = new Recorder();
    d = new GestureDetector(null, r);
    d.onTouchEvent(event(0, 0, MotionEvent.ACTION_DOWN, 100, 100));
    d.onTouchEvent(event(0, 20, MotionEvent.ACTION_MOVE, 103, 102));
    d.onTouchEvent(event(0, 40, MotionEvent.ACTION_UP, 103, 102));
    check(r.scroll == 0 && r.singleTapUp == 1, "small jitter stays a tap");

    // A fast swipe released at speed is a fling, with the velocity in pixels per second.
    r = new Recorder();
    d = new GestureDetector(null, r);
    d.onTouchEvent(event(0, 0, MotionEvent.ACTION_DOWN, 0, 0));
    d.onTouchEvent(event(0, 30, MotionEvent.ACTION_MOVE, 100, 0));
    d.onTouchEvent(event(0, 60, MotionEvent.ACTION_UP, 200, 0));
    check(r.fling == 1 && r.lastVelocityX > 1000f && r.lastVelocityX <= 8000f * 4, "fling: reported with a velocity");

    // Down, up, down again soon and close by is a double tap.
    r = new Recorder();
    d = new GestureDetector(null, r);
    d.onTouchEvent(event(0, 0, MotionEvent.ACTION_DOWN, 100, 100));
    d.onTouchEvent(event(0, 50, MotionEvent.ACTION_UP, 100, 100));
    d.onTouchEvent(event(150, 150, MotionEvent.ACTION_DOWN, 102, 101));
    d.onTouchEvent(event(150, 200, MotionEvent.ACTION_UP, 102, 101));
    check(r.doubleTap == 1 && r.doubleTapEvent >= 2, "double tap: reported with its events");

    // Too soon is a bounce, too late is a second tap.
    for (long second : new long[] {70, 450}) {
      r = new Recorder();
      d = new GestureDetector(null, r);
      d.onTouchEvent(event(0, 0, MotionEvent.ACTION_DOWN, 100, 100));
      d.onTouchEvent(event(0, 50, MotionEvent.ACTION_UP, 100, 100));
      d.onTouchEvent(event(second, second, MotionEvent.ACTION_DOWN, 100, 100));
      check(r.doubleTap == 0, "second tap at " + second + " ms is not a double tap");
    }

    // Too far away is not a double tap either.
    r = new Recorder();
    d = new GestureDetector(null, r);
    d.onTouchEvent(event(0, 0, MotionEvent.ACTION_DOWN, 100, 100));
    d.onTouchEvent(event(0, 50, MotionEvent.ACTION_UP, 100, 100));
    d.onTouchEvent(event(150, 150, MotionEvent.ACTION_DOWN, 600, 600));
    check(r.doubleTap == 0, "second tap far away is not a double tap");

    // A listener is required.
    boolean threw = false;
    try { new GestureDetector(null, (GestureDetector.OnGestureListener) null); } catch (NullPointerException e) { threw = true; }
    check(threw, "a null listener is refused");

    // Timers: show press, long press, the confirmed single tap, long press off, and cancel.
    final Recorder held = new Recorder();
    withLooper(900, looper -> {
      GestureDetector detector = new GestureDetector(null, held);
      detector.onTouchEvent(event(0, 0, MotionEvent.ACTION_DOWN, 100, 100));
    });
    check(held.showPress == 1 && held.longPress == 1, "holding reports show press then long press");

    final Recorder released = new Recorder();
    withLooper(900, looper -> {
      GestureDetector detector = new GestureDetector(null, released);
      detector.onTouchEvent(event(0, 0, MotionEvent.ACTION_DOWN, 100, 100));
      detector.onTouchEvent(event(0, 40, MotionEvent.ACTION_UP, 100, 100));
    });
    check(released.longPress == 0 && released.confirmed == 1 && released.doubleTap == 0,
        "a lone tap is confirmed after the double-tap window, with no long press");

    final Recorder off = new Recorder();
    withLooper(900, looper -> {
      GestureDetector detector = new GestureDetector(null, off);
      detector.setIsLongpressEnabled(false);
      check(!detector.isLongpressEnabled(), "long press can be turned off");
      detector.onTouchEvent(event(0, 0, MotionEvent.ACTION_DOWN, 100, 100));
    });
    check(off.longPress == 0, "no long press when disabled");

    final Recorder cancelled = new Recorder();
    withLooper(900, looper -> {
      GestureDetector detector = new GestureDetector(null, cancelled);
      detector.onTouchEvent(event(0, 0, MotionEvent.ACTION_DOWN, 100, 100));
      detector.onTouchEvent(event(0, 10, MotionEvent.ACTION_CANCEL, 100, 100));
    });
    check(cancelled.longPress == 0 && cancelled.showPress == 0 && cancelled.singleTapUp == 0, "cancel stops the timers");

    if (failures != 0) {
      System.out.println(failures + " check(s) failed");
      System.exit(1);
    }
    System.out.println("gesture detector checks passed");
  }
}
