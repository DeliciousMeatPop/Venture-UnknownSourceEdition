package android.view;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;

/**
 * Recognizes taps, double taps, long presses, scrolls and flings in a stream of single-pointer touch
 * events and reports them to listeners, with the public API of android.view.GestureDetector.
 *
 * <p>Timing and distances come from {@link ViewConfiguration}. Two simplifications: a gesture follows
 * the first pointer only (a second finger ends tap recognition), and context clicks (a stylus or
 * mouse button press) are not reported, because {@link MotionEvent} has no button actions here.
 * A PC VR session produces no touch input; this exists so apps that build touch handling into their
 * views, such as video players, load and run.
 */
public class GestureDetector {
  /** Notified of the gestures: touch down, press, tap, scroll, long press and fling. */
  public interface OnGestureListener {
    boolean onDown(MotionEvent e);
    void onShowPress(MotionEvent e);
    boolean onSingleTapUp(MotionEvent e);
    boolean onScroll(MotionEvent e1, MotionEvent e2, float distanceX, float distanceY);
    void onLongPress(MotionEvent e);
    boolean onFling(MotionEvent e1, MotionEvent e2, float velocityX, float velocityY);
  }

  /** Notified of double taps and of single taps that are not the first half of a double tap. */
  public interface OnDoubleTapListener {
    boolean onSingleTapConfirmed(MotionEvent e);
    boolean onDoubleTap(MotionEvent e);
    boolean onDoubleTapEvent(MotionEvent e);
  }

  /** Notified of context clicks. */
  public interface OnContextClickListener {
    boolean onContextClick(MotionEvent e);
  }

  /** All three listeners with empty implementations, to override only what is needed. */
  public static class SimpleOnGestureListener
      implements OnGestureListener, OnDoubleTapListener, OnContextClickListener {
    public boolean onSingleTapUp(MotionEvent e) {
      return false;
    }
    public void onLongPress(MotionEvent e) {}
    public boolean onScroll(MotionEvent e1, MotionEvent e2, float distanceX, float distanceY) {
      return false;
    }
    public boolean onFling(MotionEvent e1, MotionEvent e2, float velocityX, float velocityY) {
      return false;
    }
    public void onShowPress(MotionEvent e) {}
    public boolean onDown(MotionEvent e) {
      return false;
    }
    public boolean onDoubleTap(MotionEvent e) {
      return false;
    }
    public boolean onDoubleTapEvent(MotionEvent e) {
      return false;
    }
    public boolean onSingleTapConfirmed(MotionEvent e) {
      return false;
    }
    public boolean onContextClick(MotionEvent e) {
      return false;
    }
  }

  // A second tap closer in time than this is a bounce, not a double tap.
  private static final int DOUBLE_TAP_MIN_TIME = 40;
  // Velocity is taken over the touch samples of this many milliseconds.
  private static final long VELOCITY_WINDOW = 100;
  private static final int MAX_SAMPLES = 8;

  private final OnGestureListener listener;
  private OnDoubleTapListener doubleTapListener;
  private OnContextClickListener contextClickListener;
  private final Handler handler;
  private final int touchSlopSquare;
  private final int biggerTouchSlopSquare;
  private final int doubleTapSlopSquare;
  private final int minimumFlingVelocity;
  private final int maximumFlingVelocity;

  private boolean longpressEnabled = true;
  private boolean stillDown;
  private boolean inLongPress;
  private boolean alwaysInTapRegion;
  private boolean alwaysInBiggerTapRegion;
  private boolean deferConfirmSingleTap;
  private boolean doubleTapping;
  private boolean pendingTapConfirm;
  private boolean ignoreNextUp;
  private MotionEvent currentDown;
  private MotionEvent previousUp;
  private float downX, downY, lastX, lastY;
  private final long[] sampleTime = new long[MAX_SAMPLES];
  private final float[] sampleX = new float[MAX_SAMPLES];
  private final float[] sampleY = new float[MAX_SAMPLES];
  private int sampleCount;

  private final Runnable showPress = new Runnable() {
    public void run() {
      if (currentDown != null) listener.onShowPress(currentDown);
    }
  };
  private final Runnable longPress = new Runnable() {
    public void run() {
      if (currentDown == null) return;
      handler.removeCallbacks(confirmTap);
      pendingTapConfirm = false;
      deferConfirmSingleTap = false;
      inLongPress = true;
      listener.onLongPress(currentDown);
    }
  };
  private final Runnable confirmTap = new Runnable() {
    public void run() {
      pendingTapConfirm = false;
      if (doubleTapListener == null) return;
      if (!stillDown) doubleTapListener.onSingleTapConfirmed(currentDown);
      else deferConfirmSingleTap = true;
    }
  };

  /** A detector for events from the calling thread, with default touch distances. */
  public GestureDetector(OnGestureListener listener) {
    this(null, listener, null);
  }

  public GestureDetector(OnGestureListener listener, Handler handler) {
    this(null, listener, handler);
  }

  public GestureDetector(Context context, OnGestureListener listener) {
    this(context, listener, null);
  }

  public GestureDetector(Context context, OnGestureListener listener, Handler handler) {
    if (listener == null) throw new NullPointerException("OnGestureListener must not be null");
    this.listener = listener;
    if (listener instanceof OnDoubleTapListener) setOnDoubleTapListener((OnDoubleTapListener) listener);
    if (listener instanceof OnContextClickListener) setContextClickListener((OnContextClickListener) listener);
    if (handler != null) {
      this.handler = handler;
    } else {
      Looper looper = Looper.myLooper() != null ? Looper.myLooper() : Looper.getMainLooper();
      this.handler = new Handler(looper);
    }
    ViewConfiguration configuration = context != null ? ViewConfiguration.get(context) : new ViewConfiguration();
    int slop = configuration.getScaledTouchSlop();
    int doubleTapSlop = configuration.getScaledDoubleTapSlop();
    minimumFlingVelocity = configuration.getScaledMinimumFlingVelocity();
    maximumFlingVelocity = configuration.getScaledMaximumFlingVelocity();
    touchSlopSquare = slop * slop;
    // The region within which a second tap may still pair with the first: 2.5 times the touch slop.
    int bigger = Math.round(slop * 2.5f);
    biggerTouchSlopSquare = bigger * bigger;
    doubleTapSlopSquare = doubleTapSlop * doubleTapSlop;
  }

  public void setOnDoubleTapListener(OnDoubleTapListener listener) {
    doubleTapListener = listener;
  }

  public void setContextClickListener(OnContextClickListener listener) {
    contextClickListener = listener;
  }

  /** Whether a held touch is reported as a long press (on by default). */
  public void setIsLongpressEnabled(boolean enabled) {
    longpressEnabled = enabled;
  }

  public boolean isLongpressEnabled() {
    return longpressEnabled;
  }

  /** Feeds a touch event; true if a listener handled it. */
  public boolean onTouchEvent(MotionEvent ev) {
    final int action = ev.getActionMasked();
    final float x = ev.getX();
    final float y = ev.getY();
    boolean handled = false;
    switch (action) {
      case MotionEvent.ACTION_POINTER_DOWN:
      case MotionEvent.ACTION_POINTER_UP:
        // A second finger is not a tap or a one-finger scroll.
        alwaysInTapRegion = false;
        alwaysInBiggerTapRegion = false;
        cancelTimers();
        break;
      case MotionEvent.ACTION_DOWN: {
        if (doubleTapListener != null) {
          boolean hadTapMessage = pendingTapConfirm;
          if (hadTapMessage) {
            handler.removeCallbacks(confirmTap);
            pendingTapConfirm = false;
          }
          if (currentDown != null && previousUp != null && hadTapMessage
              && isConsideredDoubleTap(currentDown, previousUp, ev)) {
            doubleTapping = true;
            handled |= doubleTapListener.onDoubleTap(currentDown);
            handled |= doubleTapListener.onDoubleTapEvent(ev);
          } else {
            handler.postDelayed(confirmTap, ViewConfiguration.getDoubleTapTimeout());
            pendingTapConfirm = true;
          }
        }
        downX = lastX = x;
        downY = lastY = y;
        currentDown = copy(ev);
        alwaysInTapRegion = true;
        alwaysInBiggerTapRegion = true;
        stillDown = true;
        inLongPress = false;
        deferConfirmSingleTap = false;
        sampleCount = 0;
        addSample(ev);
        if (longpressEnabled) {
          handler.removeCallbacks(longPress);
          handler.postDelayed(longPress,
              ViewConfiguration.getTapTimeout() + ViewConfiguration.getLongPressTimeout());
        }
        handler.removeCallbacks(showPress);
        handler.postDelayed(showPress, ViewConfiguration.getTapTimeout());
        handled |= listener.onDown(ev);
        break;
      }
      case MotionEvent.ACTION_MOVE: {
        if (inLongPress) break;
        addSample(ev);
        final float scrollX = lastX - x;
        final float scrollY = lastY - y;
        if (doubleTapping) {
          handled |= doubleTapListener.onDoubleTapEvent(ev);
        } else if (alwaysInTapRegion) {
          final int deltaX = (int) (x - downX);
          final int deltaY = (int) (y - downY);
          final int distance = deltaX * deltaX + deltaY * deltaY;
          if (distance > touchSlopSquare) {
            handled = listener.onScroll(currentDown, ev, scrollX, scrollY);
            lastX = x;
            lastY = y;
            alwaysInTapRegion = false;
            cancelTimers();
          }
          if (distance > biggerTouchSlopSquare) alwaysInBiggerTapRegion = false;
        } else if (Math.abs(scrollX) >= 1 || Math.abs(scrollY) >= 1) {
          handled = listener.onScroll(currentDown, ev, scrollX, scrollY);
          lastX = x;
          lastY = y;
        }
        break;
      }
      case MotionEvent.ACTION_UP: {
        stillDown = false;
        MotionEvent currentUp = copy(ev);
        if (doubleTapping) {
          handled |= doubleTapListener.onDoubleTapEvent(ev);
        } else if (inLongPress) {
          handler.removeCallbacks(confirmTap);
          pendingTapConfirm = false;
          inLongPress = false;
        } else if (alwaysInTapRegion && !ignoreNextUp) {
          handled = listener.onSingleTapUp(ev);
          if (deferConfirmSingleTap && doubleTapListener != null) doubleTapListener.onSingleTapConfirmed(ev);
        } else if (!ignoreNextUp) {
          addSample(ev);
          float[] velocity = velocity();
          if (Math.abs(velocity[1]) > minimumFlingVelocity || Math.abs(velocity[0]) > minimumFlingVelocity)
            handled = listener.onFling(currentDown, ev, velocity[0], velocity[1]);
        }
        if (previousUp != null) previousUp.recycle();
        previousUp = currentUp;
        doubleTapping = false;
        deferConfirmSingleTap = false;
        ignoreNextUp = false;
        cancelTimers(false);
        break;
      }
      case MotionEvent.ACTION_CANCEL:
        cancel();
        break;
      default:
        break;
    }
    return handled;
  }

  /** Feeds a generic (non-touch) motion event; context clicks are not reported, so always false. */
  public boolean onGenericMotionEvent(MotionEvent ev) {
    return false;
  }

  private boolean isConsideredDoubleTap(MotionEvent firstDown, MotionEvent firstUp, MotionEvent secondDown) {
    if (!alwaysInBiggerTapRegion) return false;
    final long deltaTime = secondDown.getEventTime() - firstUp.getEventTime();
    if (deltaTime > ViewConfiguration.getDoubleTapTimeout() || deltaTime < DOUBLE_TAP_MIN_TIME) return false;
    int deltaX = (int) firstDown.getX() - (int) secondDown.getX();
    int deltaY = (int) firstDown.getY() - (int) secondDown.getY();
    return deltaX * deltaX + deltaY * deltaY < doubleTapSlopSquare;
  }

  private void cancelTimers() {
    cancelTimers(true);
  }

  private void cancelTimers(boolean includeTap) {
    handler.removeCallbacks(showPress);
    handler.removeCallbacks(longPress);
    if (includeTap) {
      handler.removeCallbacks(confirmTap);
      pendingTapConfirm = false;
    }
    deferConfirmSingleTap = false;
  }

  private void cancel() {
    cancelTimers();
    sampleCount = 0;
    doubleTapping = false;
    stillDown = false;
    alwaysInTapRegion = false;
    alwaysInBiggerTapRegion = false;
    deferConfirmSingleTap = false;
    if (inLongPress) inLongPress = false;
  }

  // The first pointer's position as a single-pointer event of its own.
  private static MotionEvent copy(MotionEvent e) {
    return MotionEvent.obtain(e.getDownTime(), e.getEventTime(), e.getActionMasked(), e.getX(), e.getY(),
        e.getMetaState());
  }

  private void addSample(MotionEvent e) {
    if (sampleCount == MAX_SAMPLES) {
      System.arraycopy(sampleTime, 1, sampleTime, 0, MAX_SAMPLES - 1);
      System.arraycopy(sampleX, 1, sampleX, 0, MAX_SAMPLES - 1);
      System.arraycopy(sampleY, 1, sampleY, 0, MAX_SAMPLES - 1);
      sampleCount--;
    }
    sampleTime[sampleCount] = e.getEventTime();
    sampleX[sampleCount] = e.getX();
    sampleY[sampleCount] = e.getY();
    sampleCount++;
  }

  // Pixels per second over the recent samples, limited to the maximum fling velocity.
  private float[] velocity() {
    float[] out = new float[2];
    if (sampleCount < 2) return out;
    int last = sampleCount - 1;
    int first = last;
    while (first > 0 && sampleTime[last] - sampleTime[first - 1] <= VELOCITY_WINDOW) first--;
    long elapsed = sampleTime[last] - sampleTime[first];
    if (elapsed <= 0) return out;
    out[0] = clamp((sampleX[last] - sampleX[first]) * 1000f / elapsed);
    out[1] = clamp((sampleY[last] - sampleY[first]) * 1000f / elapsed);
    return out;
  }

  private float clamp(float velocity) {
    return Math.max(-maximumFlingVelocity, Math.min(maximumFlingVelocity, velocity));
  }
}
