<?php
if(true)
{
	ini_set('display_errors', 1);
	ini_set('display_startup_errors', 1);
	error_reporting(E_ALL);
}

// print_r($_SERVER);
$here=strtok($_SERVER["REQUEST_URI"], '?');	// without query part
// echo $here;

if(isset($_GET['download']))
	{ // handle file download
	$path=$_GET['download'];
	$path="/usr/local/games/retread/".$path;
	// check if permitted... i.e. strip off any .. or initial /
	if(!file_exists($path))
		{
		header($_SERVER["SERVER_PROTOCOL"]." 404 Not Found");
		header("Status: 404 Not Found");
		$_SERVER['REDIRECT_STATUS'] = 404;
		// echo "<DOCTYPE>";
		echo "<html>";
		echo "<head>";
		echo "<title>404 Not Found</title>";
		echo "<meta name=\"generator\" content=\"retread\">";	// a hint that the script is running
		echo "</head>";
		echo "<body>";
		echo "<h1>Not Found</h1>";
		echo "<p>The requested URL ";
		echo htmlentities($_SERVER['PHP_SELF']);
		if($_SERVER['QUERY_STRING'])
			echo "?".htmlentities($_SERVER['QUERY_STRING']);
		echo " was not found on this server.</p>";
// print_r($path);
	// FIXME: optionally notify someone?
		echo "</body>";
		echo "</html>";
		exit;
		}
	header("Content-Type: application/octet-stream");
	header("Content-Length: ".filesize($path));
	header("Content-Disposition: attachment; filename=".basename($path));
//	header("Refresh: 0; url=$here");	// it is not clear if this is standard or non-standard: https://stackoverflow.com/questions/283752/refresh-http-header
	readfile($path);	// send file
	exit;
	}
if(isset($_GET['delete']))
	{ // handle file delete
	$path=$_GET['delete'];
	// check if permitted...
	// if yes, delete file
		echo "delete $path";
		exit;
	}
?>
<form method="POST" action="<?php echo $here;?>">
<img src="https://dragonbox.de/img/dragonbox-logo-15724157971.jpg"/>
<h1>Welcome to RetRead 3</h1>
<p>
<?php
echo $_SERVER['REMOTE_ADDR']." ";
echo date(DATE_RFC822)." ";
?>
<input type="submit" value="Refresh"></input>
</p>
<p>
This is just a demo of what is possible with a HTML GUI.
Alternative methods of access:
</ul>
<li>serial console login on USB (works like an FTDI adapter)</li>
<li>ssh root@192.168.0.202</li>
<li>mount smb 192.168.0.202/media</li>
</ul>
</p>
<h2>Slot Status</h2>
<?php
echo "<p>";
echo "<a href=\"$here?update=yes\">Update Game Database</a> ";
echo "<a href=\"$here?update=system\">Update Linux</a> ";
echo "<a href=\"$here?update=poweroff\">Power Off</a> ";
echo "</p>";

if(isset($_GET['update']))
{
	switch($_GET['update'])
		{
		case "yes":
			system("(wget -qO /tmp/cart_reader.zip https://codeload.github.com/sanni/cartreader/zip/refs/heads/master && unzip -q /tmp/cart_reader.zip 'cartreader-master/sd/*' -d /tmp && mv /tmp/cartreader-master/sd/* /usr/local/games/retread/test && echo Cart database updated. || echo failed.) 2>&1");
			break;
		case "system":

// FIXME: E: Could not open lock file /var/lib/apt/lists/lock - open (13: Permission denied) E: Unable to lock directory /var/lib/apt/lists/ E: Could not open lock file /var/lib/dpkg/lock - open (13: Permission denied) E: Unable to lock the administration directory (/var/lib/dpkg/), are you root?

			system("(apt-get update && apt-get upgrade && echo System updated. Now rebooting... && reboot || echo failed.) 2>&1");
			break;
		case "poweroff":

//FIXME: poweroff: must be superuser.

			system("(poweroff && echo Will power off now... || echo failed.) 2>&1");
			break;
		}
}

echo "<table border=\"1\">";
foreach(array(1 => "MegaDrive", 0 => "SNES", 2 => "NES") as $SLOT => $NAME)
	{
	echo "<tr>";
	echo "<td>".htmlentities($NAME)."</td>";
	$status=trim(file_get_contents("/sys/class/retrode3/slot$SLOT/sense"));
	echo "<td>";
	if($status == "active")
		{ // there is a Cart inserted
		$file=popen("sh -vc '/usr/local/bin/retread-info /dev/slot$SLOT'", "r");
		$str=stream_get_contents($file);
		fclose($file);
		if(!$str)
			$str="unidentified";
		echo "<a href=\"$here?slot=$SLOT&name=$NAME\">".htmlentities($str)."</a>";
		}
	else
		echo "empty";
	echo "</td>";
// fails:	system("/root/copyrom /dev/slot$SLOT /tmp/slot.bin; wc -c </tmp/slot.bin");
	// besser: ein slot-info.sh aufrufen das den retread-Befehl benutzt
	echo "</tr>";
	}

if(isset($_GET['slot']))
	{ // read out cart
	$slot="/dev/slot".$_GET['slot'];	// check for tampering with slot name e.g. 0/../..
	echo "<p>Reading ".$_GET['name']."</p>";
	echo "<p>Please wait...";
	flush();
	fclose(popen("/usr/local/bin/retread-read $slot", "r"));
	echo " ...done.</p>";
	flush();
	}

foreach(array("left", "right") as $NAME)
{
	echo "<tr>";
	echo "<td>".htmlentities("$NAME Controller")."</td>";
	echo "<td>";
	if(file_exists("/dev/input/$NAME"))
		echo "<font color=\"green\">CONNECTED</font>";
	else
		echo "not connected";
	echo "</td>";
	echo "</tr>";
}

echo "<tr>";
echo "<td>SD Card reader</td>";
echo "<td>";
$file=file("/proc/partitions");
foreach($file as $str)
	{
	//echo $str;
	if (preg_match ('/(\d+)\s+(\d+)\s+(\d+)\s+(mmcblk.p.+)/', $str, $m))
		echo $m[4]." (".ceil($m[3]/(1024*1024))." MB)<br>";	// partition name and size
//	else echo "not: ".$str."<br>";
	// could try to extract partition names, types etc.
	}
echo "</td>";
echo "</tr>";

echo "</table>";

?>
</p>
<h2>Files</h2>
<p>
<?php
# echo "<p>";
$link="smb://".$_SERVER['SERVER_ADDR']."/media";
echo "<a href=\"$link\">Open through SMB</a> ";
echo "</p>";

$path="/usr/local/games/retread";	// on Retread device
if(!file_exists($path))
	$path="/Volumes/Retrode3/retread-setup/usr/local/games/retread";	// on development host

function scansubdirs($dir)
{
	$root = scandir($dir);
	foreach($root as $value)
	{
		if($value === '.' || $value === '..') continue;
		if(!is_dir("$dir/$value"))
			{
			$result[]="$dir/$value";
			continue;
			}
		foreach(scansubdirs("$dir/$value") as $value)
			$result[]=$value;
	}
	return $result;
}

echo "<table border=\"1\">";
foreach(scansubdirs($path) as $item)
{
	echo "<tr>";
	$file=substr($item, strlen($path));
	if(is_dir($item))
		{ // directory
		echo "<td></td>";
		echo "<td>".htmlentities($file)."</td>";
		}
	else
		{ // regular file
		  // filesize($path) ausgeben
		echo "<td>".filesize($item)."</td>";
		echo "<td><a href=\"$here?download=".rawurlencode($file)."\">".htmlentities($file)."</a></td></tr>";
		}
	echo "</tr>";
}
echo "<table>";
?>
<h2>Button Status</h2>
<p>
<?php

echo "here we could read the button status of /dev/input/event0 if we have a use for it...";

?>
<h2>Select Mapper</h2>
Here could be other fancy things. Perhaps on separate pages...
<input name="mapper" type="text" width="20">
<p>
<input type="checkbox">Checkbox</input>
<p>
<input type="radio">Radio</input>
<p>
</form>
