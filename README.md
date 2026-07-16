### DISCLAIMER: AI WAS USED TO GENERATE/MODIFY THIS CODE, USE AT YOUR OWN RISK
I just had AI make this for my own personal use.


Modified this code using Gemini to allow it to open the target browser window before opening the link.
I believe this is the only part that creates the difference -> [link](https://github.com/anventia/glazewm-link-router/blob/b02bae6abc1dd722c5c2fa94543dd9df08d21313/mod.wh.cpp#L207-L217)
in case the original author wants to add it to the main mod.

By default if a window is in another GlazeWM workspace and hidden (not shown in any monitor), opening a link would open a new browser window
Now this mod will open your current window first, so it opens in the same window.

Not tested extensively
- what if two windows of the same browser are open?
