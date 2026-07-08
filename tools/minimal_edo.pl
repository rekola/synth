#!/usr/bin/perl

use strict;
use warnings;
use autodie;
use utf8;

use POSIX qw(pow);
use Getopt::Long;

main();

sub main {
    my $find_minimal;
    my $list_intervals;
    my $edo;
    
    GetOptions(
	minimal => \$find_minimal,
	intervals => \$list_intervals,
	"edo=i" => \$edo,
	) or exit 1;

    my @ratios = (
	'1:1',	
	'16:15',
	'15:14',
	'14:13',
	'13:12',
	'12:11',
	'11:10',
	'10:9',
	'9:8',
	'8:7',
	'7:6',
	'6:5',
	'5:4',
	'4:3',
	'3:2',
	'8:5',
	'5:3',
	'12:7',
	'7:4',
	'16:9',
	'9:5',
	'20:11',
	'11:6',
	'24:13',
	'13:7',
	'28:15',
	'15:8',
	);

    my @extra_ratios = (
	'33:32',
	'32:31',
	'31:30',
	'30:29',
	'29:28',
	'28:27',
	'27:26',
	'26:25',
	'25:24',
	'24:23',
	'23:22',
	'22:21',
	'21:20',
	'20:19',
	'19:18',
	'18:17',
	'17:16',
	);

    if ($find_minimal) {
	for (my $edo = 1; $edo < 200; $edo++) {
	    my %notes;
	    for my $r (@ratios) {
		my $f = ratio_to_frequency($r);
		$notes{get_note($f, $edo)} = 1;
	    }
	    if (scalar keys %notes >= 21) {
		print STDERR $edo . "edo: notes = " . (scalar keys %notes) . " / " . (scalar @ratios) . "\n";
	    }
	}
    } else {
	die "no edo specified" if !$edo;
	my @ratios_per_note;
	push @ratios_per_note, [ ] for 1..$edo;
	for my $r (@ratios, @extra_ratios) {
	    my $f = ratio_to_frequency($r);
	    my $note = get_note($f, $edo);
	    push @{$ratios_per_note[$note]}, $r;
	}

	for (my $note = 0; $note < $edo; $note++) {
	    my $nr = $ratios_per_note[$note];
	    print STDOUT $note . "\t" . (join ', ', @$nr) . "\n";
	}
    }
}

sub ratio_to_frequency {
    my $r = shift;
    die "error" if $r !~ /^(\d+):(\d+)$/;
    return $1 / $2;
}

sub get_note {
    my ($frequency, $edo) = @_;

    $frequency *= 440.0;

    my ($best_note, $best_d);
    
    for (my $i = 0; $i < $edo; $i++) {
	my $note_frequency = get_frequency($i, $edo);
	my $d = abs($frequency - $note_frequency);
	if (!defined $best_note || $d < $best_d) {
	    $best_note = $i;
	    $best_d = $d;
	}
    }
    return $best_note;
}

sub get_frequency {
    my ($note, $edo) = @_;
    
    return 440.0 * pow(2.0, $note / $edo);
}


