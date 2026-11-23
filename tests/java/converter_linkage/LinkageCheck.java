package com.venture.dex;

import java.util.HashMap;
import java.util.Map;
import java.util.Set;

/** Which verification failures stop an app from starting (Linkage.deferred). The messages are
 *  HotSpot's own wording, taken from a converted APK that bundles copies of the JDK's internals. */
public final class LinkageCheck {
    static int failures;
    static final Set<String> FRAMEWORK = Set.of("android/view/View", "android/app/Activity");

    static Map<String, Object> failure(String stage, String kind, String detail) {
        Map<String, Object> f = new HashMap<>();
        f.put("stage", stage);
        f.put("kind", kind);
        f.put("detail", detail);
        f.put("class", "app.Example");
        return f;
    }

    static void check(boolean expected, String what, Map<String, Object> f) {
        boolean actual = Linkage.deferred(f, FRAMEWORK);
        if (actual != expected) {
            failures++;
            System.out.println("FAIL: " + what + ": expected " + (expected ? "deferred" : "fatal") + ", got " + (actual ? "deferred" : "fatal"));
        }
    }

    public static void main(String[] args) {
        String hs = "hotspot";
        String iae = "java.lang.IllegalAccessError";
        // Deferred: they fail only when reached, as on ART.
        check(true, "missing framework method", failure("framework-member", "java.lang.NoSuchMethodError", "android/view/View.foo()V"));
        check(true, "missing frame hierarchy", failure("frames", null, "missing frame hierarchy: app/Missing"));
        check(true, "app class in java.* package", failure(hs, "java.lang.SecurityException", "Prohibited package name: java.awt"));
        check(true, "missing class", failure(hs, "java.lang.NoClassDefFoundError", "app/Gone"));
        check(true, "class that does not verify", failure(hs, "java.lang.VerifyError", "Bad type on operand stack"));
        check(true, "package-private super in another app package",
            failure(hs, iae, "class app.a.Child cannot access its superclass app.b.Stub"));
        check(true, "super in a JDK package the module does not export (java.base)",
            failure(hs, iae, "superclass access check failed: class com.sun.net.ssl.internal.www.protocol.https.Handler (in unnamed module @0x5955568) "
                + "cannot access class sun.net.www.protocol.https.Handler (in module java.base) because module java.base does not export "
                + "sun.net.www.protocol.https to unnamed module @0x5955568"));
        check(true, "interface in a JDK package the module does not export (java.xml)",
            failure(hs, iae, "superinterface check failed: class com.sun.org.apache.bcel.internal.generic.InstructionComparator$1 (in unnamed module @0x5955568) "
                + "cannot access class com.sun.org.apache.bcel.internal.generic.InstructionComparator (in module java.xml) because module java.xml "
                + "does not export com.sun.org.apache.bcel.internal.generic to unnamed module @0x5955568"));
        check(true, "subclass of a JDK class that is final here",
            failure(hs, "java.lang.IncompatibleClassChangeError", "class sun.java2d.pipe.Region$ImmutableRegion cannot inherit from final class sun.java2d.pipe.Region"));

        // Fatal: these are Venture's own gaps or real defects.
        check(false, "inaccessible framework superclass (old wording)",
            failure(hs, iae, "class app.Child cannot access its superclass android.view.View"));
        check(false, "inaccessible framework class (module wording)",
            failure(hs, iae, "superclass access check failed: class app.Child (in unnamed module @0x1) cannot access class android.app.Activity "
                + "(in module x) because module x does not export android.app to unnamed module @0x1"));
        check(false, "framework class that is final here",
            failure(hs, "java.lang.IncompatibleClassChangeError", "class app.Child cannot inherit from final class android.view.View"));
        check(false, "other class-change error",
            failure(hs, "java.lang.IncompatibleClassChangeError", "Class app.A does not implement the requested interface app.B"));
        check(false, "other access error",
            failure(hs, iae, "class app.A tried to access private method 'void app.B.hidden()'"));
        check(false, "other security error",
            failure(hs, "java.lang.SecurityException", "signer information does not match signer information of other classes in the same package"));
        check(false, "frames failure that is not a missing hierarchy", failure("frames", null, "inconsistent stackmap frames at app/A.run"));
        check(false, "unknown stage", failure("pipeline", null, "boom"));

        if (failures != 0) {
            System.out.println(failures + " check(s) failed");
            System.exit(1);
        }
        System.out.println("linkage checks passed");
    }
}
