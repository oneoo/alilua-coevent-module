<?php
function microtime_float()
{
    list($usec, $sec) = explode(" ", microtime());
    return ((float)$usec + (float)$sec);
}
$time_start = microtime_float();

$curl = curl_init();
curl_setopt_array($curl, array(
    CURLOPT_RETURNTRANSFER => 1,
    CURLOPT_URL => 'http://wiki.upyun.com/index.php?title=%E9%A6%96%E9%A1%B5'
));

$result = curl_exec($curl);
print(strlen($result)."\n");

$time_end = microtime_float();
$time = $time_end - $time_start;
echo "Did nothing in $time seconds\n";


function multiple_threads_request($nodes){ 
        $mh = curl_multi_init(); 
        $curl_array = array(); 
		$res = array();
        foreach($nodes as $i => $url) 
        { 
			$res[$i] = 0;
            $curl_array[$i] = curl_init($url); 
            curl_setopt($curl_array[$i], CURLOPT_RETURNTRANSFER, true); 
            curl_multi_add_handle($mh, $curl_array[$i]); 
        } 
        $running = NULL; 
        do { 
            curl_multi_exec($mh,$running); 
        } while($running > 0); 
        
        foreach($nodes as $i => $url) 
        { 
            $res[$i] += strlen(curl_multi_getcontent($curl_array[$i])); 
        } 
        
        foreach($nodes as $i => $url){ 
            curl_multi_remove_handle($mh, $curl_array[$i]); 
        } 
        curl_multi_close($mh);        
        return $res; 
}
$time_start = microtime_float();
print_r(multiple_threads_request(array( 
    'http://wiki.upyun.com/index.php?title=%E9%A6%96%E9%A1%B5', 
    'http://wiki.upyun.com/index.php?title=%E9%A6%96%E9%A1%B5', 
    'http://wiki.upyun.com/index.php?title=%E9%A6%96%E9%A1%B5', 
    'http://wiki.upyun.com/index.php?title=%E9%A6%96%E9%A1%B5', 
    'http://wiki.upyun.com/index.php?title=%E9%A6%96%E9%A1%B5', 
    'http://wiki.upyun.com/index.php?title=%E9%A6%96%E9%A1%B5', 
    'http://wiki.upyun.com/index.php?title=%E9%A6%96%E9%A1%B5', 
    'http://wiki.upyun.com/index.php?title=%E9%A6%96%E9%A1%B5', 
    'http://wiki.upyun.com/index.php?title=%E9%A6%96%E9%A1%B5', 
    'http://wiki.upyun.com/index.php?title=%E9%A6%96%E9%A1%B5', 
)));
$time_end = microtime_float();
$time = $time_end - $time_start;
echo "Did nothing in $time seconds\n";