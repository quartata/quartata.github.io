use strict;
use warnings;

use MIME::Base64;

my @replacements =
(
    ["//!SPIRAX", "assets/spirax.o.woff2", 1],
    ["//!SCP", "assets/source_code_pro.o.woff2", 1],
    ["//!AVATAR", "assets/avatar.png", 1],
);


sub slurp($)
{
    open (my $fh, "<", shift) or die;

    local $/;
    my $str = <$fh>;

    close $fh;
    return $str;
}


my $input = slurp $ARGV[0];

for my $rule (@replacements)
{
    my ($match, $file, $base64) = @$rule;

    my $replacement = slurp $file;

    if ($base64)
    {
        $replacement = encode_base64 $replacement, "";
    }

    $input =~ s/\Q$match/$replacement/g;
}

open (my $output, ">", $ARGV[1]) or die;
print $output $input;
close $output;
