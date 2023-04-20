<?php

/**
 * @file
 * Provision/Drush hooks for git commands.
 */

/**
 * Implementation of hook_drush_command().
 */
function provision_git_drush_command() {
  $items['provision-git-tag'] = array(
    'description' => 'Create a new git tag.',
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
    'arguments' => array(
      'git_tag' => dt('The desired name of the tag.'),
    ),
    'options' => array(
      'message' => 'The message to attach to the tag.',
      'checkout' => "When done creating the tag, check it out so the repository stays on that tag.",
      'push' => "Push the tag to the repository remote 'origin'.",
    )
  );
  $items['provision-git-branch'] = array(
    'description' => 'Create a new git branch.',
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
    'arguments' => array(
      'git_ref' => dt('Name of the new branch.'),
    ),
    'options' => array(
      'push' => "Optional: Push the new branch to the remote origin.",
    )
  );
  $items['provision-git-push'] = array(
    'description' => 'Executes "git push".',
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
    'aliases' => array('push'),
    'options' => array(
      'repo_path' => 'Optional: force a repository path. Defaults to the site or platform dir.',
      'force_repo_path_as_toplevel' => 'Stop Git from looking in parent directories. Defaults to FALSE.',
    ),
  );
  $items['provision-git-commit'] = array(
    'description' => 'Commits all added and changed files to the repository.',
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
    'aliases' => array('commit'),
    'options' => array(
      'message' => 'Optional: The commit message to use. Defaults to a generated message.',
      'push' => 'Push the code after committing. Defaults to FALSE',
      'files' => 'Optional: A list of files to commit.',
      'name' => 'Optional: The name of the user to commit as.',
      'mail' => 'Optional: The email of the user to commit as.',
      'repo_path' => 'Optional: force a repository path. Defaults to the site or platform dir.',
      'force_repo_path_as_toplevel' => 'Stop Git from looking in parent directories. Defaults to FALSE.',
    ),
  );
  return $items;
}

/**
 * Implements the provision-git-push command.
 */
function drush_provision_git_push() {
  provision_process('git push', d()->root, dt('Pushing to git repository...'));
}

/**
 * Implements the provision-git-commit command.
 */
function drush_provision_git_commit() {
  $git_path = d()->root;
  $git_url = d()->git_remote;

  $files = json_decode(drush_get_option('files', '[]'));
  $push = drush_get_option('push', FALSE);
  $name = drush_get_option('name', 'unknown');
  $mail = drush_get_option('mail', 'unknown');
  $hostmaster_uri = d('hostmaster')->uri;

  // If files option was detected
  if (!empty($files)) {
    $files_list = '';

    foreach ($files as $file) {
      $file = $git_path . '/' . $file;
      if (file_exists($file)) {
        $files_list .= ' ' . $file;
      }
    }

    // If files lists is not empty, add "git add" command to queue.
    if (!empty($files_list)) {
      $cmd = "git add {$files_list}";
    }
  }
  // If no files are specified, add everything,
  else {
    $cmd = 'git add -A';
  }

  if ($cmd) {
    provision_process($cmd, $git_path, dt('Adding files'));
  }

  provision_process('git status', $git_path, dt('Git Status'));

  $target = d()->name;

  //Generate commit message
  $message = array();
  $message[] = drush_get_option('provision_git_commit_message', 'Provision Git Commit');
  $length = strlen(end($message));

  if ($hostmaster_uri) {
    $message[] = "Committed on site $target, by $name using $hostmaster_uri";
    $length = strlen(end($message));
  }
  if (drush_get_option('message')) {
    $message[] = str_repeat('=', $length);
    $message[] = drush_get_option('message');
  }

  // Make commit using environment variables for NAME and EMAIL, if specified.
  $env = array();
  if (!empty($name) || !empty($mail)) {
    $message[] = str_repeat('=', $length);
    $message[] = "Committed by user $name <$mail>";
  }
  if (!empty($name)) {
    $env['GIT_AUTHOR_NAME'] = $name;
    $env['GIT_COMMITTER_NAME'] = $name;
  }
  if (!empty($mail)) {
    $env['GIT_AUTHOR_MAIL'] = $mail;
    $env['GIT_COMMITTER_MAIL'] = $mail;
  }

  // Implode message array and prepare for command insertion.
  $message = escapeshellarg(implode("\n", $message));

  // Show git status before commit
  provision_process('git status', $git_path, dt('Git Status'));

  // Unfortunately, git exits with
  // a status of 1 if there are no files to commit. This causes
  // drush_shell_cd_and_exec() to return FALSE and we think the
  // commit failed. So we have to resort to some hackery. We scan
  // the output and count how many files don't starts with '#'.
  // if the count is 1, then we have to assume there was nothing
  // to commit.

  // Run commit process with environment variables.
  $output = provision_process("git commit -m {$message}", $git_path, 'Committing changes', $env, TRUE, NULL, FALSE);

  $process = drush_get_context('provision_process_result');
  if (!$process->isSuccessful()) {
    $comments = 0;
    $lines = 0;
    $lines_array = explode("\n", $output);
    foreach ($lines_array as $line) {
      if (strpos($line, '#') === 0) {
        $comments++;
      }
      else {
        $lines++;
      }
    }
    if ($comments > 0 && $lines == 1) {
      drush_log(dt('Nothing to commit.'), 'warning');
    }
    elseif (strpos($output, 'nothing to commit, working directory clean') === FALSE) {
      return drush_set_error('DRUSH_PROVISION_GIT_COMMIT_FAILED', $output);
    }
  }

  // Push the result, if desired.
  if ($push) {

    // Do a little pre-check to ensure we don't hang at http password.
    $git_url = parse_url($git_url);
    if (($git_url['scheme'] == 'http' || $git_url['scheme'] == 'https') && empty($git_url['pass'])) {
      drush_log('http git url detected, with no password. Unable to push to this repository.', 'warning');
      return;
    }
    else {
      provision_process("git push", $git_path, 'Pushing code to remote repository...');
      provision_process('git status', $git_path, dt('Git Status'));
    }
  }
}

/**
 * Implements the provision-git-add command.
 */
function drush_provision_git_add($path) {
  if (empty($path)) {
    throw new \Exception(dt('Path argument cannot be empty.'));
  }
  provision_process("git add {$path}", d()->root);
}

/**
 * Implements the provision-git-branch command
 */
function drush_provision_git_branch($git_ref) {

  $path = d()->root;

  // Make sure there is a git ref
  if (empty($git_ref)) {
    return drush_set_error('DRUSH_PROVISION_GIT_CHECKOUT_FAILED', dt("Git checkout failed: No git_ref specified."));
  }
  provision_process("git fetch", $path);

  $git_ref_arg = trim($git_ref);
  $command = "git checkout -b $git_ref_arg";
  provision_process($command, $path, t('Creating branch...'));
  drush_set_option('git_reference', $git_ref_arg);

  if (drush_get_option('push')) {
    provision_process("git push --set-upstream origin $git_ref_arg", $path, dt('Pushing branch to origin...'));
  }
}

/**
 * Implements the provision-git-tag command
 */
function drush_provision_git_tag($git_tag = '') {
  $path = d()->root;

  // Make sure there is a git ref
  if (empty($git_tag)) {
    return drush_set_error('DRUSH_PROVISION_GIT_TAG_FAILED', dt("Git tag failed: Must specify a tag name."));
  }

  $command = "git tag " . escapeshellarg(trim($git_tag));
  if (drush_get_option('message', '')) {
    $command .= ' -m ' . escapeshellarg(drush_get_option('message'));
  }

  provision_process("git fetch", $path);

  // Try to perform the git checkout.
  provision_process($command, $path, 'Creating tag...');

  // If 'checkout' was specified, checkout the tag.
  if (drush_get_option('checkout', FALSE)) {
    provision_process("git checkout $git_tag", $path, 'Checking out tag...');
    drush_set_option('git_reference', $git_tag);
  }

  // If 'push' was specified, run git push.
  if (drush_get_option('push', FALSE)) {
    provision_process("git push origin $git_tag", $path, 'Pushing tag...');
  }
}

/**
 * Register our directory as a place to find provision classes.
 */
function provision_git_register_autoload() {
  static $loaded = FALSE;
  if (!$loaded) {
    $loaded = TRUE;
    provision_autoload_register_prefix('Provision_', dirname(__FILE__));
  }
}

/**
 * Implements hook_drush_init().
 */
function provision_git_drush_init() {
  provision_git_register_autoload();
}
