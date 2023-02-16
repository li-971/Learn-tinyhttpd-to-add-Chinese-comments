#!/usr/local/bin/perl -Tw
# 执行此代码后，将会生成一个拥有标题“Example CGI script”的HTML页面，页面背景颜色为红色。该页面上将包含文本“CGI Example”和给出给当前运行脚本的参数列表。
use strict;
use CGI;

my($cgi) = new CGI;

print $cgi->header('text/html');
print $cgi->start_html(-title => "Example CGI script",
                       -BGCOLOR => 'red');
print $cgi->h1("CGI Example");
print $cgi->p, "This is an example of CGI\n";
print $cgi->p, "Parameters given to this script:\n";
print "<UL>\n";
foreach my $param ($cgi->param)
{
 print "<LI>", "$param ", $cgi->param($param), "\n";
}
print "</UL>";
print $cgi->end_html, "\n";
