use constant SHORTTEXT_LIMIT => 75;
my $max_line_length = 100;
		    (($line =~ m@^\s+diff\b.*a/([\w/]+)@ &&
		      $line =~ m@^\s+diff\b.*a/[\w/]+\s+b/$1\b@) ||
		if ($realfile eq '' && !$has_patch_separator && $line =~ /^\s*change-id:/i) {
			ERROR("GERRIT_CHANGE_ID",
			      "Remove Gerrit Change-Id's before submitting upstream\n" . $herecurr);
		}
# Check for line lengths > 75 in commit log, warn once
		    length($line) > 75 &&
			     "Possible unwrapped commit description (prefer a maximum 75 chars per line)\n" . $herecurr);
			if (defined($id) &&
			   ($short || $long || $space || $case || ($orig_desc ne $description) || !$hasparens)) {
				ERROR("GIT_COMMIT_ID",
				      "Please use git commit description style 'commit <12+ chars of sha1> (\"<title line>\")' - ie: '${init_char}ommit $id (\"$description\")'\n" . $herecurr);
			}
		if (!$reported_maintainer_file && !$in_commit_log &&
		    ($line =~ /^(?:new|deleted) file mode\s*\d+\s*$/ ||
		     $line =~ /^rename (?:from|to) [\w\/\.\-]+\s*$/ ||
		     ($line =~ /\{\s*([\w\/\.\-]*)\s*\=\>\s*([\w\/\.\-]*)\s*\}/ &&
		      (defined($1) || defined($2))))) {
			$is_patch = 1;
			$reported_maintainer_file = 1;
			WARN("FILE_PATH_CHANGES",
			     "added, moved or deleted file(s), does MAINTAINERS need updating?\n" . $herecurr);
		}
		if ($line =~ /\b(?:BUG|BUG_ON)\b/) {
			my $msg_level = \&WARN;
			$msg_level = \&CHK if ($file);
			&{$msg_level}("AVOID_BUG",
				      "Avoid crashing the kernel - try using WARN_ON & recovery code rather than BUG() or BUG_ON()\n" . $herecurr);
		}
		if ($line =~ /\bLINUX_VERSION_CODE\b/) {
			WARN("LINUX_VERSION_CODE",
			     "LINUX_VERSION_CODE should be avoided, code should be for the version to which it is merged\n" . $herecurr);
		}
				$fixed_line =~ /(^..*$Type\s*$Ident\(.*\)\s*)\{(.*)$/;