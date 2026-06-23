package RangeList;

use strict;
use warnings;
use bigint;

use Storable qw(dclone);

use Exporter qw(import);
our @EXPORT_OK = qw(str_to_rl add_range rl_union show_rl);

sub text_to_int($)
{
    my $text = shift;

    if ($text =~ /s64min/) {
        return -(2**63);
    } elsif ($text =~/s32min/) {
        return -(2**31);
    } elsif ($text =~ /s16min/) {
        return -(2**15);
    } elsif ($text =~ /s64max/) {
        return 2**63 - 1;
    } elsif ($text =~ /s32max/) {
        return 2**31 - 1;
    } elsif ($text =~ /s16max/) {
        return 2**15 - 1;
    } elsif ($text =~ /u64max/) {
        return 2**64 - 1;
    } elsif ($text =~ /u32max/) {
        return 2**32 - 1;
    } elsif ($text =~ /u16max/) {
        return 2**16 - 1;
    } elsif ($text =~ /ptr_max/) {
        # FIXME: add 32 bit support
        return 2**64 - 4096;
    }

    if ($text =~ /\((.*?)\)/) {
        $text = $1;
    }
    if (!($text =~ /^[-0123456789]/)) {
        return "NaN";
    }

    return int($text);
}

sub add_range($$$)
{
    my $union = shift;
    my $min = shift;
    my $max = shift;
    my %range;
    my @return_union = ();
    my $added = 0;
    my $check_next = 0;

    $range{min} = $min;
    $range{max} = $max;

    foreach my $tmp (@$union) {
        if ($added) {
            push @return_union, $tmp;
            next;
        }

        if ($range{max} < $tmp->{min}) {
            push @return_union, \%range;
            push @return_union, $tmp;
            $added = 1;
        } elsif ($range{min} <= $tmp->{min}) {
            if ($range{max} <= $tmp->{max}) {
                $range{max} = $tmp->{max};
                push @return_union, \%range;
                $added = 1;
            }
        } elsif ($range{min} <= $tmp->{max}) {
            if ($range{max} <= $tmp->{max}) {
                push @return_union, $tmp;
                $added = 1;
            } else {
                $range{min} = $tmp->{min};
            }
        } else {
            push @return_union, $tmp;
        }
    }

    if (!$added) {
        push @return_union, \%range;
    }

    return \@return_union;
}

sub print_num($)
{
    my $num = shift;

    if ($num < 0) {
        return "(" . $num . ")";
    } else {
        return $num;
    }
}

sub print_range($)
{
    my $range = shift;

    if ($range->{min} == $range->{max}) {
        return print_num($range->{min});
    } else {
        return print_num($range->{min}) . "-" .  print_num($range->{max});
    }
}

sub show_rl($)
{
    my $union = shift;
    my $printed_range = "";
    my $i = 0;

    if ($#$union > 100) {
	return "min-max";
    }

    foreach my $range (@$union) {
        if ($i) {
            $printed_range = $printed_range . ",";
        }
        $i++;
        $printed_range = $printed_range . print_range($range);
    }
    return $printed_range;
}

sub str_to_rl($)
{
    my $str = shift;

    $str =~ s/\[.*//;

    my $rl = ();
    my ($min, $max);

    my @ranges = split(/,/, $str);
    foreach my $range_txt (@ranges) {
        if ($range_txt =~ /(.*[^(])-(.*)/) {
            $min = text_to_int($1);
            $max = text_to_int($2);
        } else {
            $min = text_to_int($range_txt);
            $max = $min;
        }
        if ($min =~ /NaN/ || $max =~ /NaN/) {
            $min = 2**63 - 1;
            $max = -(2**63);
        }
        $rl = add_range($rl, $min, $max);
    }
    return $rl;
}

sub rl_union($$)
{
    my $one = shift;
    my $two = shift;
    my $res = dclone($one);

}

1;
