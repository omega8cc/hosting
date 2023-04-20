<?php


/**
 * Implements task confirm form for "git-branch" task.
 */
function hosting_task_git_branch_form($node) {

  $form['current_git_ref'] = array(
    '#type' => 'item',
    '#title' => t('Current Git Reference'),
    '#markup' => $node->git['git_ref'],
    '#field_prefix' => '<code>',
    '#field_suffix' => '</code>',
    '#description' => t('The current git reference for this !type.', array('!type' => $node->type)),
  );

  $form['git_ref'] = array(
    '#title' => t('New Branch Name'),
    '#description' => t("Enter a new branch name to create from the current working copy."),
    '#type' => 'textfield',
    '#default_value' => '',
    '#element_validate' => ['hosting_git_checkout_git_ref_validate'],
    '#attributes' => [
      'placeholder' => 'new-git-branch'
    ],
  );

  $form['push'] = array(
    '#title' => t('Push the new branch.'),
    '#description' => t("Push the new branch to the remote repository."),
    '#type' => 'checkbox',
    '#default_value' => TRUE,
  );

  drupal_set_title(t('Create a git branch.'));
  return $form;
}

/**
 * Implements task confirm form for "git-commit" task.
 */
function hosting_task_git_commit_form($node) {

  global $user;
  $options = array();

  if ($node->type == 'platform') {
    $repo_path = $node->git['repo_path'];
  } elseif ($node->type == 'site') {
    $repo_path = $node->publish_path . '/sites/' . $node->title;
  }

  try {
    $wrapper = new GitWrapper();
    $git = $wrapper->workingCopy($repo_path);
    $diff_stat = array_filter(explode(PHP_EOL,$wrapper->git('diff --numstat', $repo_path)));
    $status = array_filter(explode(PHP_EOL, $git->getStatus()));

    // Generate list of files
    foreach ($status as &$line) {
      $items = array_filter(explode(' ', trim($line)));
      $file  = array_combine(array('status', 'path'), $items);

      if ($file['status'] == 'A') {
        $options[$file['path']] = '<span class="text-success">Added</span>';
      }
      elseif ($file['status'] == 'M') {
        $options[$file['path']] = '<span class="text-info">Modified</span>';
      }
      elseif ($file['status'] == 'D') {
        $options[$file['path']] = '<span class="text-danger">Deleted</span>';
      }
      elseif ($file['status'] == '??') {
        $options[$file['path']] = '<span class="text-muted">New</span>';
      }
      $options[$file['path']] .= "<code class='text-muted'>{$file['path']}</code>";
    }
  }
  catch (GitException $e) {
    drupal_set_message("Cannot Commit Changes: Unable to load git repository at path '{$repo_path}'. Something is wrong: either the path in the database is wrong, or the files are missing from their intended location.", 'error');
    drupal_set_message($e->getMessage(), 'error');
  }
  $form['git_info'] = array(
    '#type' => 'fieldset',
    '#title' => t('Git Status & Diff'),
    '#collapsible' => TRUE,
    '#collapsed' => TRUE,
    '#weight' => 100,
    '#prefix' => '<div class="alert alert-info">',
    '#suffix' => t('It is recommended to read the git diff before you commit.') . '</div>',
  );

  // Limit status to 1000 lines
  $output = shell_exec("cd $repo_path; git -c color.ui=always  status");
  $lines = explode("\n", $output);
  $count = count($lines);
  if ($count > 100) {
    $lines = array_slice($lines, 0, 100);
    $lines[] = "# STATUS TRUNCATED. SHOWING 100 of $count LINES.";
  }
  $git_status  = implode("\n", $lines);

  // Get git diff.
  $git_diff = trim(shell_exec("cd $repo_path; git -c color.ui=always diff"));

  $form['git_info']['status'] = array(
    '#type' => 'markup',
    '#markup' => theme('devshop_ascii', array(
      'output' => $git_status
    )),
  );
  $form['git_info']['diff'] = array(
    '#type' => 'markup',
    '#markup' => theme('devshop_ascii', array(
      'output' => $git_diff
    )),
  );
  $form['files'] = array(
    '#description' => t('Check the boxes next to the files you wish to commit.  If you leave all boxes unchecked, all new and changed files will be committed.'),
    '#type' => 'checkboxes',
    '#options' => $options,
    '#element_validate' => array('hosting_git_commit_validate_files'),
  );
  $form['message'] = array(
    '#type' => 'textarea',
    '#title' => t('Git Commit Message'),
    '#description' => t('Enter a message to use as the git commit message.'),
    '#rows' => 2,
  );
  $form['push'] = array(
    '#title' => t('Push to Remote'),
    '#description' => t('After updating and committing, push the changes to the git remote.'),
    '#type' => 'checkbox',
    '#default_value' => 1,
  );
  $form['name'] = array(
    '#type' => 'value',
    '#value' => $user->name,
  );
  $form['mail'] = array(
    '#type' => 'value',
    '#value' => $user->mail,
  );
  return $form;
}

/**
 * Converts list of files to json encode for hosting task arguments table.
 * @param $element
 * @param $form_state
 */
function hosting_git_commit_validate_files($element, &$form_state) {
  if (!empty($element['#value'])) {
    $values = json_encode(array_filter(array_values($element['#value'])));
    form_set_value($element, $values, $form_state);
  }
}

/**
 * Implements task confirm form for "git-tag" task.
 */
function hosting_task_git_tag_form($node) {

  $form['git_ref'] = array(
    '#title' => t('Tag'),
    '#description' => t("Enter the tag you wish to create.        "),
    '#type' => 'textfield',
    '#default_value' => isset($_GET['git_tag'])? $_GET['git_tag']: '',
    '#attributes' => array(
      'placeholder' => 'v1.0.0',
    )
  );

  $form['push'] = array(
    '#title' => t('Push Tag to remote'),
    '#description' => t("Push the tag to the repository's remote.     "),
    '#type' => 'checkbox',
    '#default_value' => TRUE,
  );

  $form['checkout'] = array(
    '#title' => t('Checkout Tag'),
    '#description' => t("When done creating the tag, check it out so the code stays at this tag."),
    '#type' => 'checkbox',
    '#default_value' => TRUE,
  );

  return $form;
}
