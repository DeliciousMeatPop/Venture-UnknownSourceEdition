package com.venture.dex;

import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** Which verification failures of a converted APK are deferred linkage: they fail only when the
 *  class or method is reached, as on ART, so they must not stop the app from starting. */
final class Linkage {
    private Linkage() {}

    static final Pattern INACCESSIBLE_SUPER = Pattern.compile("cannot access its (?:abstract )?super(?:class|interface) (\\S+)");
    // HotSpot's module system refuses an app class that extends or implements a class in a package
    // its JDK module does not export. A library that bundles a copy of the JDK's own internals
    // (a Blu-ray player's java.awt peers, an XML stack's com.sun classes) has such classes. ART has no
    // modules and loads a class only when it is used, so they never fail unless the app reaches them.
    static final Pattern UNEXPORTED_SUPER = Pattern.compile(
        "cannot access class (\\S+) \\(in module \\S+\\) because module \\S+ does not export");
    // The same bundled copies can subclass a JDK class that is final in this JDK.
    static final Pattern FINAL_SUPER = Pattern.compile("cannot inherit from final class (\\S+)");

    static boolean deferred(Map<?, ?> f, Set<String> frameworkClasses) {
        // A missing framework method or field fails only the call that reaches it, as on ART.
        if ("framework-member".equals(f.get("stage"))) return true;
        if ("frames".equals(f.get("stage"))) return str(f, "detail").contains("missing frame hierarchy:");
        if ("hotspot".equals(f.get("stage"))) {
            // A class that does not verify fails when it is reached, as ART
            // fails a method when it runs; bundled SDK code a VR game never
            // reaches must not stop the game.
            Object kind = f.get("kind");
            String detail = str(f, "detail");
            // HotSpot refuses app classes in java.* packages, which ART loads;
            // Stremio's libVLC bundles Blu-ray menu classes in java.awt and java.io.
            if ("java.lang.SecurityException".equals(kind)) return detail.startsWith("Prohibited package name:");
            // ART links a class when it is first used and fails it then. An app class that cannot
            // access another app class (Alcove's media-compat classes extend a package-private
            // Stub in another package) is the app's own; an inaccessible framework class is ours.
            if ("java.lang.IllegalAccessError".equals(kind)) {
                Matcher m = INACCESSIBLE_SUPER.matcher(detail);
                boolean found = m.find();
                if (!found) {
                    m = UNEXPORTED_SUPER.matcher(detail);
                    found = m.find();
                }
                return found && !frameworkClasses.contains(m.group(1).replace('.', '/'));
            }
            if ("java.lang.IncompatibleClassChangeError".equals(kind)) {
                Matcher m = FINAL_SUPER.matcher(detail);
                return m.find() && !frameworkClasses.contains(m.group(1).replace('.', '/'));
            }
            return "java.lang.NoClassDefFoundError".equals(kind) || "java.lang.ClassNotFoundException".equals(kind)
                || "java.lang.VerifyError".equals(kind);
        }
        return false;
    }

    private static String str(Map<?, ?> map, String key) {
        Object value = map.get(key);
        return value == null ? "" : value.toString();
    }
}
