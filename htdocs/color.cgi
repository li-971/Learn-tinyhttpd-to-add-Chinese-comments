#!/usr/local/bin/perl -Tw
# 执行此代码后，将会生成一个拥有标题为color（如果用户输入color参数）的HTML页面。该页面的背景颜色也将是用户提供的color值，并且包含一个文本“This is color”（color为用户提供的color值）。
# perl 
use strict;
use CGI;

my($cgi) = new CGI;

print $cgi->header;
my($color) = "blue";
$color = $cgi->param('color') if defined $cgi->param('color');

print $cgi->start_html(-title => uc($color),
                       -BGCOLOR => $color);
print $cgi->h1("This is $color");
print $cgi->end_html;
