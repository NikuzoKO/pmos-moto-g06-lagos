// lagos: always ask sites for their mobile version (Firefox for Android UA;
// mobile-config-firefox defaults to "X11; Linux x86_64; Mobile", which most
// sites serve the desktop page to). Bump the version with Firefox upgrades.
user_pref("general.useragent.override", "Mozilla/5.0 (Android 15; Mobile; rv:154.0) Gecko/154.0 Firefox/154.0");
// honour <meta name="viewport"> like mobile Firefox does
user_pref("dom.meta-viewport.enabled", true);
