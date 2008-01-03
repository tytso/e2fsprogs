<?php
// Generic Source Forge Web Page Template
// Modified from the Source Forge Default Page (v 1.2) with the 
// following changes:
// 
// 1.  Use the small SourceForge icon with the project group id so that the 
//     project gets "credit" for hits to its page.
//
// 2.  Change the "no content yet" message with an include of a 
//	project-specific content file.
//
// 3.  Change copyright statement to remove (C) VA Linux systems (since 
//	this is project-specific content.
//
//$headers = getallheaders();	// Why was this being run in the default page?

// The config.inc file should define the following two variables:
// content_file, which contains the file which should be included to get
// the actual content for this web page, and group_id, which defines the 
// SourceForge group id, so that web hits to the page get counted for the 
// most active project statistics.  An example config.inc file might 
// look like this:
//
// < ? php
// $content_file = "serial.inc";
// $group_id = 310;
// ? >
//
// (Note the space between the angle brackets and the question mark was added
// to prevent PHP from getting confused when parsing the comments in this 
// PGP page. Remove the space when creating your config.inc file.)
//
include "config.inc"; 
?>
<HTML>
<HEAD>
<TITLE><?php print "$title" ?></TITLE>
</HEAD>

<BODY bgcolor=#FFFFFF topmargin="0" bottommargin="0" leftmargin="0" rightmargin="0" marginheight="0" marginwidth="0">

<!-- top strip -->
<TABLE width="100%" border=0 cellspacing=0 cellpadding=2 bgcolor="737b9c">
  <TR>
    <TD><SPAN class=maintitlebar>&nbsp;&nbsp;
      <A class=maintitlebar href="http://sourceforge.net/"><B>Home</B></A> | 
      <A class=maintitlebar href="http://sourceforge.net/about.php"><B>About</B></A> | 
      <A class=maintitlebar href="http://sourceforge.net/partners.php"><B>Partners</B></a> |
      <A class=maintitlebar href="http://sourceforge.net/contact.php"><B>Contact Us</B></A> |
      <A class=maintitlebar href="http://sourceforge.net/account/logout.php"><B>Logout</B></A></SPAN></TD>
    </TD>
  </TR>
</TABLE>
<!-- end top strip -->

<!-- top title table -->
<TABLE width="100%" border=0 cellspacing=0 cellpadding=0 bgcolor="" valign="center">
  <TR valign="center" bgcolor="#eeeef8">
    <TD>
      <A href="http://sourceforge.net"> 
      <IMG src="http://sourceforge.net/sflogo.php?group_id=<?php print "$group_id" ?>&type=1" width="1" height="1" border="0">
	<IMG src="http://sourceforge.net/images/sflogo2-steel.png" width="143" height="70" border="0"></A>
    </TD>
    <TD width="99%"><!-- right of logo -->
      <a href="http://www.valinux.com"><IMG src="http://sourceforge.net/images/valogo3.png" align="right" alt="VA Linux Systems" hspace="5" vspace="7" border=0 width="117" height="70"></A>
    </TD><!-- right of logo -->
  </TR>
  <TR><TD bgcolor="#543a48" colspan=2><IMG src="http://sourceforge.net/images/blank.gif" height=2 vspace=0></TD></TR>
</TABLE>
<!-- end top title table -->

<!-- center table -->
<TABLE width="100%" border="0" cellspacing="0" cellpadding="10" bgcolor="#FFFFFF" align="center">
  <TR>
    <TD>
	<?php include($content_file) ?>

    </TD>
  </TR>
</TABLE>
<!-- end center table -->

<?php 
	if (file_exists("banner.inc")) {
		include "banner.inc";
	}
?>

<!-- footer table -->
<TABLE width="100%" border="0" cellspacing="0" cellpadding="2" bgcolor="737b9c">
  <TR>
    <TD align="center"><FONT color="#ffffff"><SPAN class="titlebar">
      All trademarks and copyrights on this page are properties of their respective owners.</SPAN></FONT>
    </TD>
  </TR>
</TABLE>

<!-- end footer table -->
</BODY>
</HTML>
