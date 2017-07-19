{{$header.tpl}}

<p><code>
  #include &lt;{{File_full_path}}&gt;<br>
  namespace <a href="{{File_package_url}}">{{File_namespace_name}}</a>
</code></p>

<p>{{File_brief}}</p>
{{File_details}}

<table width="100%">
  <tr>
    <th colspan="2">
      <h3 style="margin-top: 4px">Classes in this file</h3>
    </th>
  </tr>
  {{#File_class_type}}
    <tr>
      <td>
        <div><code><a href="#{{Class_anchor}}">{{Class_name}}</a></code></div>
        <div style="font-style: italic; margin-top: 4px; margin-left: 16px;">
          {{Class_brief}}
        </div>
      </td>
    </tr>
  {{/File_class_type}}
</table>

{{#File_has_member}}
<table>
  <tr>
    <th colspan="2">
      <h3 style="margin-top: 4px">File Members</h3>
      <div style="font-style: italic; font-weight: normal;">
        These definitions are not part of any class.
      </div>
    </th>
  </tr>
  {{#File_member}}
    {{$brief_member.tpl}}
  {{/File_member}}
</table>
{{/File_has_member}}

{{#File_member}}
  {{$detailed_member.tpl}}
{{/File_member}}

{{#File_class_type}}
  {{$class.tpl}}
{{/File_class_type}}
{{$footer.tpl}}
