# SDL's Java methods are invoked from native code by name via JNI,
# so R8 must not remove or rename them.
-keep class org.libsdl.app.** { *; }

# Luanti's own Activity/Service/Provider classes are also JNI touchpoints.
-keep class cn.luanti.luanti.** { *; }
